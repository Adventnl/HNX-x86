# Synchronization primitives

`kernel/sync/` provides the kernel's concurrency toolkit: atomics, spinlocks (with
IRQ-save variants), wait queues, and the sleeping primitives built on them —
mutex, reader-writer lock, counting semaphore, and completion. Two consumers of
these primitives, the deferred work queue (`kernel/work/`) and the timer callout
wheel (`kernel/time/`), are documented at the end because they are part of Work
Unit A and depend directly on the spinlock.

The kernel is currently single-CPU, so most of these are really guards against
re-entrancy from interrupt handlers rather than true multi-core mutual exclusion.
The API is nonetheless written to be SMP-correct (atomic exchange in the spinlock
body, sequentially-consistent atomics, memory fences) so the code does not need
rewriting when SMP arrives.

## Architecture

```
   mutex   rwlock   semaphore   completion        <- sleeping primitives
      \       |        |          /
       +------+--------+---------+
                  |  block/wake on
                  v
              waitqueue   ----park current thread---->  scheduler
                  |  protected by                       (THREAD_BLOCKED/READY,
                  v                                       scheduler_reschedule,
              spinlock (+irqsave)                         scheduler_make_ready)
                  |  built on
                  v
               atomic_t  (__atomic builtins, SEQ_CST)
```

- **`atomic.h`** is the bottom: thin wrappers over the compiler `__atomic`
  builtins. Everything above uses `atomic_t` or the spinlock.
- **`spinlock`** is the non-sleeping mutual-exclusion primitive. Its IRQ-save
  variant (disable interrupts, take lock) is the common case on a UP kernel.
- **`waitqueue`** is the single blocking primitive. Every sleeping object
  (`mutex`, `rwlock`, `semaphore`, `completion`) parks contended waiters on a
  wait queue and wakes them through the scheduler.

The blocking integration is the key design point: `waitqueue_wait` links an
on-stack `wait_entry`, sets the current thread to `THREAD_BLOCKED`, and calls
`scheduler_reschedule()` with interrupts off; a waker sets the thread back to
`THREAD_READY` and calls `scheduler_make_ready(thread)`. The low-level
enqueue/dequeue ops are independent of the scheduler so they can be unit-tested
with `thread == NULL`.

## File map

| File | Purpose |
|------|---------|
| `kernel/sync/atomic.h` | `atomic_t`/`atomic64_t` and SEQ_CST ops (load/store/add/sub/inc/dec/cas/xchg), fences, `cpu_relax` |
| `kernel/sync/spinlock.h` | `spinlock` struct, init/lock/trylock/unlock/held, `*_irqsave`/`*_irqrestore` |
| `kernel/sync/spinlock.c` | test-and-set lock body, IRQ-save via `irq_save_flags_and_disable` |
| `kernel/sync/waitqueue.h` | `wait_entry`, `waitqueue`, low-level enqueue/dequeue + `waitqueue_wait`/`wake_*` |
| `kernel/sync/waitqueue.c` | the scheduler-integrated park/wake implementation |
| `kernel/sync/mutex.h` / `mutex.c` | sleeping binary mutex with owner tracking |
| `kernel/sync/rwlock.h` / `rwlock.c` | writer-preferring reader-writer lock |
| `kernel/sync/semaphore.h` / `semaphore.c` | counting semaphore (P/V) |
| `kernel/sync/completion.h` / `completion.c` | one-shot/re-armable event signal |
| `kernel/work/workqueue.h` / `workqueue.c` | deferred (function, arg) work queue |
| `kernel/time/callout.h` / `callout.c` | one-shot + periodic timer callouts (sorted list + wheel) |
| `kernel/tests/sync_tests.c` | spinlock/waitqueue/mutex/sem/completion/rwlock self-tests |
| `kernel/tests/workqueue_tests.c` | work queue self-tests (marker `workqueue tests`) |
| `kernel/tests/timer_tests.c` | timer callout self-tests (marker `timer callout tests`) |

## Data structures

### `atomic_t` / `atomic64_t` (`atomic.h`)

```c
typedef struct { volatile int32_t v; } atomic_t;
typedef struct { volatile int64_t v; } atomic64_t;
#define ATOMIC_INIT(x) { (x) }
```

Wrappers so an atomic cannot be accessed non-atomically by accident. All ops use
`__ATOMIC_SEQ_CST` (fences `smp_rmb`/`smp_wmb` use ACQUIRE/RELEASE). `cpu_relax`
emits `pause`.

### `struct spinlock` (`spinlock.h`)

```c
struct spinlock {
    atomic_t    locked;        /* 0 = free, 1 = held */
    const char *name;
    uint64_t    owner_cpu;     /* reserved for SMP   */
    uint64_t    acquisitions;  /* statistics         */
    uint64_t    contended;     /* fast-path misses   */
};
#define SPINLOCK_INIT(nm) { ATOMIC_INIT(0), (nm), 0, 0, 0 }
```

### `struct wait_entry` / `struct waitqueue` (`waitqueue.h`)

```c
struct wait_entry {
    struct list_node link;
    struct thread   *thread;   /* parked thread (NULL in pure-list tests) */
    int              woken;    /* set by the waker */
};
struct waitqueue {
    struct list_node waiters;
    struct spinlock  lock;
    uint64_t         wakeups;  /* statistics */
};
```

A `wait_entry` is allocated on the blocking thread's stack and linked into the
queue before it deschedules — no allocation on the sleep path.

### `struct mutex` (`mutex.h`)

```c
struct mutex {
    atomic_t          locked;        /* 0 = free, 1 = held */
    struct thread    *owner;
    struct waitqueue  waiters;
    const char       *name;
    uint64_t          acquisitions, contended;
};
```

Owner-tracked so misuse (unlock by non-owner) is detectable and so
`mutex_owner()` works.

### `struct rwlock` (`rwlock.h`)

```c
struct rwlock {
    int               state;           /* -1 writer, 0 free, >0 reader count */
    int               waiting_writers;
    struct spinlock   lock;
    struct waitqueue  readers, writers;
    const char       *name;
};
```

A single signed `state` encodes the lock: `0` free, `> 0` is N concurrent readers,
`-1` is one exclusive writer. `waiting_writers` drives writer preference.

### `struct semaphore` (`semaphore.h`)

```c
struct semaphore {
    int               count;
    int               max;     /* informational ceiling (0 = unbounded) */
    struct spinlock   lock;
    struct waitqueue  waiters;
    const char       *name;
};
```

### `struct completion` (`completion.h`)

```c
struct completion {
    int               done;    /* sticky until reset */
    struct spinlock   lock;
    struct waitqueue  waiters;
    const char       *name;
};
```

### `struct work` / `struct workqueue` (`workqueue.h`)

```c
struct work {
    struct list_node link;
    void  (*fn)(struct work *self, void *arg);
    void   *arg;
    int     pending;
    uint64_t run_count;
};
struct workqueue {
    struct list_node items;
    struct spinlock  lock;
    const char      *name;
    uint64_t         queued, executed;
};
```

### `struct callout` / `struct callout_base` (`callout.h`)

```c
struct callout {
    struct list_node sorted_link;  /* global sorted list position */
    struct list_node wheel_link;   /* its wheel bucket            */
    uint64_t  expires;             /* absolute tick of next fire  */
    uint64_t  period;              /* 0 = one-shot                */
    void    (*fn)(struct callout *, void *);
    void     *arg;
    int       armed;
    uint64_t  fire_count;
};
struct callout_base {
    struct list_node sorted;                      /* ascending by expires      */
    struct list_node wheel[CALLOUT_WHEEL_SIZE];   /* 256 buckets               */
    uint64_t now;
    size_t   armed_count;
    uint64_t total_fired;
};
```

## Key APIs

### Atomics (`atomic.h`)

```c
int32_t atomic_read/set/add/sub/inc/dec(a [, delta/val]);
int     atomic_cas(a, expected, desired);   /* 1 if swapped */
int32_t atomic_xchg(a, val);
/* 64-bit mirror: atomic64_* */
void    smp_mb/rmb/wmb(void);  void cpu_relax(void);
```

### Spinlock (`spinlock.c`)

```c
void     spinlock_init(l, name);
void     spinlock_lock(l);          /* atomic_xchg to 1; spins on miss */
int      spinlock_trylock(l);       /* 1 on success, 0 if held         */
void     spinlock_unlock(l);        /* wmb then store 0                 */
int      spinlock_held(l);
uint64_t spinlock_lock_irqsave(l);  /* disable IRQs, lock, return flags */
void     spinlock_unlock_irqrestore(l, flags);
```

`spinlock_lock` does `atomic_xchg(&locked, 1)`; if the previous value was non-zero
it bumps `contended` and spins with `cpu_relax`. On a UP kernel that spin is only
reachable if an interrupt handler took the lock without the IRQ-save variant —
which is why the IRQ-save form is the rule, not the exception. `acquisitions` is
incremented on every successful acquire. `spinlock_lock_irqsave` saves RFLAGS and
disables interrupts (`irq_save_flags_and_disable`) before locking; the matching
restore unlocks then restores RFLAGS, so nested critical sections re-enable
interrupts only when the outermost section exits.

### Wait queue (`waitqueue.c`)

```c
void   waitqueue_init(wq, name);
int    waitqueue_empty(wq);  size_t waitqueue_len(wq);
/* low-level (no scheduler): */
void               waitqueue_enqueue(wq, e);
struct wait_entry *waitqueue_dequeue(wq);     /* FIFO, NULL if empty */
void               waitqueue_remove(wq, e);
/* high-level (real threads only): */
void waitqueue_wait(wq);            /* park current thread until woken */
int  waitqueue_wake_one(wq);        /* wake oldest; returns count */
int  waitqueue_wake_all(wq);
```

`waitqueue_wait` is the heart of the blocking integration:

```c
void waitqueue_wait(struct waitqueue *wq) {
    struct wait_entry e; list_init(&e.link);
    e.thread = thread_current(); e.woken = 0;
    uint64_t flags = irq_save_flags_and_disable();
    list_add_tail(&e.link, &wq->waiters);
    if (e.thread) e.thread->state = THREAD_BLOCKED;
    scheduler_reschedule();              /* returns here once made ready  */
    if (list_linked(&e.link)) list_del_init(&e.link);
    irq_restore_flags(flags);
}
```

It enqueues with interrupts disabled, marks the thread blocked, and reschedules; it
returns only after a waker made the thread ready again, then ensures the entry is
unlinked. The waker side (`wake_entry`) sets `woken = 1`, increments `wakeups`, and
if the entry has a real thread sets it `THREAD_READY` and calls
`scheduler_make_ready(thread)`. The low-level `enqueue`/`dequeue`/`remove` take the
queue's spinlock with `irqsave` and do not touch the scheduler, which is exactly how
the boot-time test drives them with `thread == NULL`.

### Mutex (`mutex.c`)

```c
void mutex_init(m, name);
int  mutex_trylock(m);   /* atomic_cas(0->1); records owner */
void mutex_lock(m);      /* trylock, else park-and-retry loop */
void mutex_unlock(m);    /* clear owner, store 0, wake one */
int  mutex_is_locked(m); struct thread *mutex_owner(m);
```

`mutex_lock` first tries `mutex_trylock` (a `0->1` CAS that records
`owner = thread_current()`); on contention it bumps `contended` and loops
`while (!mutex_trylock(m)) waitqueue_wait(&m->waiters);`. `mutex_unlock` clears the
owner, stores 0, and `waitqueue_wake_one`. The retry-after-wake loop (rather than
direct hand-off) means a freshly woken waiter races other acquirers and simply
re-parks if it loses — correct but not strictly FIFO-fair.

### Reader-writer lock (`rwlock.c`)

```c
void rwlock_init(rw, name);
void rwlock_read_lock(rw);    int rwlock_read_trylock(rw);    void rwlock_read_unlock(rw);
void rwlock_write_lock(rw);   int rwlock_write_trylock(rw);   void rwlock_write_unlock(rw);
int  rwlock_reader_count(rw); int rwlock_write_held(rw);
```

**Writer-preferring.** `rwlock_read_trylock` succeeds only when
`state >= 0 && waiting_writers == 0` — readers are deferred while any writer waits,
so a stream of readers cannot starve a writer. A reader increments `state`; a writer
needs `state == 0` and sets it to `-1`. `rwlock_write_lock` first increments
`waiting_writers` (blocking new readers immediately), spins via the writers' wait
queue until it wins `write_trylock`, then decrements `waiting_writers`.
`rwlock_write_unlock` resets `state = 0` and hands off to a waiting writer first
(`wake_one(writers)`), or, if none, wakes all readers (`wake_all(readers)`).
`rwlock_read_unlock` decrements `state` and, if it reached 0 with writers waiting,
wakes one writer.

### Semaphore (`semaphore.c`)

```c
void semaphore_init(s, initial, max, name);
void semaphore_down(s);     /* P: trydown, else park-and-retry */
int  semaphore_trydown(s);  /* 1 if count>0 (decrement), else 0 */
void semaphore_up(s);       /* V: increment (capped at max), wake one */
int  semaphore_count(s);
```

`semaphore_up` caps `count` at `max` when `max > 0` (a bounded resource counter);
`max == 0` means unbounded. `down` loops `trydown`/`waitqueue_wait`.

### Completion (`completion.c`)

```c
void completion_init(c, name);  void completion_reset(c);
void wait_for_completion(c);            /* loops until done */
int  try_wait_for_completion(c);        /* 1 if done, non-blocking */
void complete(c);                       /* set done, wake one */
void complete_all(c);                   /* set done, wake all */
int  completion_done(c);
```

`done` is sticky: if the event fired before the waiter arrives,
`wait_for_completion` returns immediately. `completion_reset` re-arms it.

### Work queue (`workqueue.c`)

```c
void work_init(w, fn, arg);
void workqueue_init(wq, name);
int  workqueue_queue(wq, w);          /* 1 if newly queued, 0 if already pending */
int  workqueue_cancel(wq, w);         /* 1 if removed before running */
int  workqueue_run_pending(wq);       /* run & dequeue all; returns count run */
int  workqueue_pending_count(wq);
```

Each item runs at most once per drain; re-queueing a still-pending item is a no-op
(`pending` guard). `workqueue_run_pending` pops items under the spinlock but runs
the callback with the lock **dropped**, so a callback may safely re-queue work
(which the next loop iteration picks up). `run_count`/`executed` track invocations.

### Timer callouts (`callout.c`)

```c
void callout_base_init(b);  void callout_init(c, fn, arg);
void callout_arm(b, c, delay);                 /* one-shot at now+delay */
void callout_arm_periodic(b, c, delay, period);
int  callout_cancel(b, c);                     /* 1 if it was armed */
int  callout_advance(b, now);                  /* fire all due; returns count */
uint64_t callout_next_expiry(b);  size_t callout_armed(b);
```

Each armed callout lives in **both** an authoritative ascending sorted list and a
256-bucket hashed wheel (`expires & CALLOUT_WHEEL_MASK`). `callout_advance(now)`
fires everything with `expires <= now` in expiry order off the front of the sorted
list, re-arming periodic callouts at `expires + period` (relative to the scheduled
expiry, so no drift; a large time jump produces one firing per elapsed period and
leaves the timer armed strictly in the future).

## Single-CPU semantics

- **Spinlocks guard against IRQ re-entrancy, not other CPUs.** On UP, the only way a
  lock is contended is an interrupt handler grabbing it while the main path holds
  it; the IRQ-save variants prevent that by disabling interrupts across the critical
  section. The plain `spinlock_lock` body still uses `atomic_xchg` so it is correct
  if SMP later makes contention real.
- **Atomics are SEQ_CST.** On a single core these compile to plain instructions plus
  compiler barriers; the ordering is free but the API is SMP-ready.
- **Wait/wake ordering relies on interrupts-off.** `waitqueue_wait` enqueues and
  marks blocked with interrupts disabled before `scheduler_reschedule`, closing the
  lost-wakeup window that would otherwise exist between "decide to sleep" and
  "actually sleep".
- **`owner_cpu` is reserved.** The field exists in `struct spinlock` for a future
  SMP build but is unused today.

## Invariants

- **Spinlock:** `locked` is 0 (free) or 1 (held); `acquisitions` counts successful
  acquires; `spinlock_unlock` issues a release fence before clearing.
- **Mutex:** `locked == 1` iff `owner != NULL` while held; `mutex_unlock` clears the
  owner before releasing; a contended `mutex_lock` only returns owning the lock.
- **Rwlock:** `state` is `-1`, `0`, or a positive reader count — never both readers
  and a writer; a reader cannot acquire while `waiting_writers > 0` (writer
  preference); `reader_count() == max(state, 0)`; `write_held() == (state < 0)`.
- **Semaphore:** `count >= 0`; `count <= max` when `max > 0`; each `down` that
  returns has consumed exactly one unit.
- **Completion:** `done` is monotonic until `completion_reset` clears it; a waiter
  arriving after `complete` never blocks.
- **Waitqueue:** dequeue is FIFO (oldest first); `wake_*` returns the number of
  entries actually woken; a real-thread entry is `THREAD_READY` after wake.
- **Workqueue:** a `work` is on at most one queue at a time (`pending` guard);
  `run_count` increments once per execution; callbacks run with the lock dropped.
- **Callout:** an armed callout is linked in both the sorted list and a wheel bucket;
  `armed_count` equals the number of armed callouts; the sorted list head is always
  the earliest expiry; periodic re-arm leaves `expires > now`.

## Failure modes

- **Calling `waitqueue_wait` before the scheduler exists.** It marks the current
  thread blocked and reschedules; in the boot context (no runnable threads) this
  would hang. The header explicitly warns: the high-level wait/wake path is for real
  threads only. Boot self-tests therefore use the low-level enqueue/dequeue with
  `thread == NULL`.
- **Spinlock held with interrupts on, then an IRQ takes it.** The handler spins
  forever (self-deadlock on UP). Always use `spinlock_lock_irqsave` for any lock an
  interrupt handler can touch.
- **Mutex unlock by non-owner / recursive lock.** `mutex_lock` is non-recursive;
  the owner re-locking deadlocks. `owner` is tracked so such misuse is detectable in
  debugging, but `mutex_unlock` does not enforce ownership at runtime.
- **Writer starvation is *prevented* but reader throughput suffers.** Because new
  readers defer while a writer waits, a steady writer stream can stall readers — the
  intended trade-off of writer preference.
- **Semaphore over-up.** `up` past `max` is clamped (no error), so a buggy extra
  `up` is silently absorbed rather than raising the count unboundedly.
- **Workqueue starvation via self-requeue.** A callback that unconditionally
  re-queues itself makes `workqueue_run_pending` loop until the (re-added) item is no
  longer pending; an always-pending self-requeue would not terminate. Tests only
  re-queue a *different* item.
- **Callout callback re-arming the same callout.** Supported (the wheel/list are
  updated), but a periodic callout whose `period` is 0 after firing simply does not
  re-arm; a zero period means one-shot.

## Verification

All three suites run in `kernel_core_tests_run()` and are gated by
`make verify-kernel-core-expanded` (hence `make verify-production-200k`).

| Suite (file) | Marker(s) |
|--------------|-----------|
| `sync_tests.c` :: `test_spinlock` | `[PASS] spinlock tests` |
| `sync_tests.c` :: `test_waitqueue` | `[PASS] wait queue tests` |
| `sync_tests.c` :: `test_mutex` (also semaphore + completion) | `[PASS] mutex tests` |
| `sync_tests.c` :: `test_rwlock` | `[PASS] rwlock tests` |
| `workqueue_tests.c` :: `workqueue_tests_run` | `[PASS] workqueue tests` |
| `timer_tests.c` :: `timer_tests_run` | `[PASS] timer callout tests` |

`verify-kernel-core-expanded` (`Makefile.production`) `--expect`s exactly these
markers: `wait queue tests`, `mutex tests`, `rwlock tests`, `workqueue tests`, and
`timer callout tests` (the spinlock suite is run alongside but the verify target
keys on the others).

Which paths the boot-time tests exercise — and which they deliberately don't:

- **Exercised (deterministic, single-threaded):** spinlock lock/trylock/unlock,
  IRQ-save round-trip and the `acquisitions` counter; the **low-level**
  waitqueue enqueue/dequeue/remove with `thread == NULL` and FIFO `wake_all`; mutex
  uncontended lock/owner/trylock; semaphore down/up to exhaustion and the `max`
  cap; completion already-done fast return and reset; rwlock multiple readers,
  writer-blocked-by-readers, exclusive writer, reader-blocked-by-writer, and
  second-writer-blocked; workqueue queue/double-queue/run/cancel and re-queue from a
  running callback; the full callout matrix (out-of-order arm, ordered firing,
  cancel, periodic re-arm, missed-tick catch-up) over a virtual clock.
- **Not exercised at boot:** the **blocking** paths — `waitqueue_wait`,
  `mutex_lock`'s contended park, `semaphore_down`'s block, `rwlock`'s contended
  park, `wait_for_completion`'s block. These require a running scheduler and real
  threads, so they are integration-tested later rather than in the single-threaded
  boot context (as the suite comments note).

## Future expansion

- **SMP correctness.** Populate `owner_cpu`, add per-CPU run queues to the
  scheduler, and audit each primitive's fences; the SEQ_CST atomics and
  release/acquire fences are already in place for the transition.
- **Priority inheritance.** `mutex` tracks `owner` but does not boost it; adding PI
  would prevent priority inversion once the scheduler grows priorities.
- **Fair hand-off.** Replace the park-and-retry loops in `mutex`/`semaphore` with
  direct ownership transfer to the woken waiter for strict FIFO fairness.
- **Timed waits.** Add `*_timeout` variants by combining `waitqueue_wait` with a
  `callout` that wakes the waiter, giving bounded blocking for I/O.
- **Dedicated worker threads.** Today `workqueue_run_pending` is drained by whatever
  context calls it; a kworker thread plus `complete()` hand-off would turn it into a
  true asynchronous deferred-execution facility.
- **Hierarchical timer wheel.** The single 256-bucket wheel handles near-future
  expiries; cascading wheels would keep `callout_advance` cheap for far-future
  timers without scanning the sorted list.
- **Lock-order checking.** `spinlock.name` is captured for a future deadlock/order
  validator (mentioned in `spinlock.h`); recording acquisition order in debug builds
  would catch lock-ordering bugs automatically.
