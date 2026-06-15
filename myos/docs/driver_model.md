# Driver Model

The driver core is HNX/MyOS's uniform driver/device model. Every bus and class
layer — PCI, block, char, input, storage — registers into it, so a single
`devices` enumeration can list everything the kernel has discovered, regardless
of which subsystem owns it. On top of the registry sits an explicit device
lifecycle (probe / remove / suspend / resume / reset), an ACPI-style power-state
model, a hardware **event bus** that records lifecycle transitions, and a
diagnostics snapshot.

This is grounded in `kernel/driver/`, `kernel/device/`, and `kernel/hw/`. The
generic model is deliberately bus-agnostic: PCI class/vendor matching lives in
the PCI layer (`docs/pci_deep.md`), and the generic core only matches by
`enum device_type`.

## Architecture

```
            driver_register(struct driver*)       device_register(struct device*)
                      |                                    |
                      v                                    v
            g_drivers (list)                       g_devices (list, tail-append)
                      \                                    /
                       \---- driver_probe_all() ----------/   (type match + probe())
                                      |
              ---------------------------------------------------------
              |                 lifecycle dispatchers                  |
   driver_probe  driver_remove  driver_suspend  driver_resume  driver_reset
              |        |             |               |              |
        device_set_state / device_power_set        device_reset_power_cycle
              |
        hw_event_emit(...) ---> hw_event_bus ring (64) + running total
                                      |
                              hw_diag_collect / hw_diag_dump  (snapshot for `hwinfo`)
```

Two registries hold the model:

- **Drivers** are a singly-linked stack (`g_drivers`, prepend on register).
- **Devices** are a singly-linked queue (`g_devices`/`g_devices_tail`,
  append on register), with a running `g_device_count`.

There are two matching paths, and they are distinct:

1. `driver_probe_all()` (in `driver_registry.c`) is the simple "bind everything"
   loop: for each unbound device, try each same-type driver's `probe()`, bind the
   first that returns 0. It does **not** touch lifecycle state or emit events.
2. `driver_probe(dev)` (in `driver_lifecycle.c`) is the full lifecycle dispatch
   for a single device: it walks the device through DISCOVERED → MATCHED →
   ACTIVE, sets power state D0 on success, and emits `driver-bound` /
   `driver-failed` events.

The lifecycle entry points (`driver_probe`, `_remove`, `_suspend`, `_resume`,
`_reset`) are the contract; the driver hooks are optional side effects. A driver
with no `suspend`/`resume`/`reset` still transitions cleanly — the state machine
runs, the hook is simply skipped.

### Boot wiring

From `kernel/src/kernel.c`:

```c
/* Prompt 5 */
driver_core_init();           /* driver_registry_init + "[OK] Driver core online" */
pci_init();
pci_register_devices();       /* mirror PCI functions as DEV_TYPE_PCI devices    */
block_init();                 /* block devices mirror as DEV_TYPE_BLOCK on register */
...
/* Prompt 6 */
driver_lifecycle_init();      /* "[OK] Driver lifecycle online" */
hw_event_bus_init();          /* "[OK] Hardware event bus online" */
driver_tests_run();           /* mock device through the full state machine */
```

Note the ordering quirk: `hw_event_bus_init()` runs *after*
`driver_lifecycle_init()` but the event bus guards emits with a `g_ready` flag,
so any `hw_event_emit` before the bus is up is silently dropped rather than
crashing.

## File map

| File | Role |
| --- | --- |
| `kernel/driver/driver.h` | `struct driver`, `struct device`, `enum device_type`, `enum device_state`, `enum driver_probe_result`; lifecycle prototypes. |
| `kernel/driver/driver.c` | `device_init_struct`, `driver_core_init`. |
| `kernel/driver/driver_registry.h` | Registry + probe-all API; `driver_registry_head`. |
| `kernel/driver/driver_registry.c` | The two linked lists, `driver_register`/`device_register`, `driver_probe_all`, dumps, iterators. |
| `kernel/driver/driver_lifecycle.c` | `driver_probe`/`_remove`/`_suspend`/`_resume`, `device_set_state`, `device_state_name`, `driver_lifecycle_init`. |
| `kernel/driver/device_id.h` / `device_id.c` | `enum device_type` taxonomy, `struct device_id`, `device_type_name`. |
| `kernel/device/device.h` / `device.c` | Char-device registry (separate flat table); `device_init` registers null/zero. |
| `kernel/device/char_device.h` / `char_device.c` | `struct char_device`, dispatch, `/dev/null` and `/dev/zero` built-ins. |
| `kernel/hw/power/device_power.h` / `device_power.c` | `enum device_power_state` (D0–D3), `device_power_set`/`_get`/`_state_name`. |
| `kernel/hw/power/device_reset.h` / `device_reset.c` | `device_reset_power_cycle` (D3→D0) and the `driver_reset` dispatcher. |
| `kernel/hw/events/hw_event.h` / `hw_event.c` | `struct hw_event`, `enum hw_event_type`, `hw_event_type_name`. |
| `kernel/hw/events/hw_event_bus.h` / `hw_event_bus.c` | 64-entry ring + total; `hw_event_emit`/`_count`/`_recent`/`_dump_recent`. |
| `kernel/hw/diagnostics/hw_diag.h` / `hw_diag.c` | `struct hw_diag_summary`, `hw_diag_collect`/`hw_diag_dump`. |
| `kernel/tests/driver_tests.c` | Lifecycle + event-bus self-test (mock driver/device). |

## Data structures

### `struct driver` (`driver.h`)

```c
enum driver_probe_result {
    DRIVER_PROBE_OK      =  0,   /* claimed                       */
    DRIVER_PROBE_NOMATCH = -1,   /* not this driver's device      */
    DRIVER_PROBE_ERROR   = -2,   /* matched but failed to init    */
};

struct driver {
    const char      *name;
    enum device_type type;
    int (*probe)  (struct device *dev);   /* 0 to claim, negative to decline */
    int (*remove) (struct device *dev);
    int (*suspend)(struct device *dev);
    int (*resume) (struct device *dev);
    int (*reset)  (struct device *dev);
    struct driver *next;                   /* registry link */
};
```

### `struct device` (`driver.h`)

```c
#define DEVICE_NAME_MAX 32

struct device {
    char                    name[DEVICE_NAME_MAX];
    enum device_type        type;
    struct device_id        id;          /* vendor/device/class/subclass/prog_if */
    void                   *bus_data;    /* e.g. struct pci_device* / block_device* */
    void                   *drv_data;    /* driver-private state */
    struct driver          *driver;      /* bound driver, or NULL */
    enum device_state       state;       /* lifecycle state */
    enum device_power_state power_state; /* power state */
    struct device          *next;        /* registry link */
};
```

### Lifecycle + power states

```c
enum device_state {
    DEVICE_STATE_DISCOVERED = 0,
    DEVICE_STATE_MATCHED,
    DEVICE_STATE_INITIALIZED,            /* defined; not currently entered by the dispatchers */
    DEVICE_STATE_ACTIVE,
    DEVICE_STATE_SUSPENDED,
    DEVICE_STATE_FAILED,                 /* defined; reserved */
    DEVICE_STATE_REMOVED,
};

enum device_power_state {               /* ACPI-style D-states (device_power.h) */
    DEV_POWER_D0 = 0,   /* fully powered / active */
    DEV_POWER_D1,       /* light sleep */
    DEV_POWER_D2,       /* deeper sleep */
    DEV_POWER_D3,       /* off / suspended */
};
```

`DEVICE_STATE_INITIALIZED` and `DEVICE_STATE_FAILED` are part of the taxonomy but
the current dispatchers move DISCOVERED → MATCHED → ACTIVE directly on a
successful probe; `D1`/`D2` are likewise defined but unused by the generic core
(suspend uses D3, resume uses D0).

### Device taxonomy (`device_id.h` / `device_id.c`)

```c
enum device_type {
    DEV_TYPE_BUS = 0, DEV_TYPE_PCI, DEV_TYPE_BLOCK, DEV_TYPE_CHAR,
    DEV_TYPE_INPUT, DEV_TYPE_CONSOLE, DEV_TYPE_STORAGE, DEV_TYPE_COUNT,
};

struct device_id {
    uint32_t vendor;       /* 0xFFFF = any */
    uint32_t device;       /* 0xFFFF = any */
    uint8_t  class_code, subclass, prog_if;
};
```

`device_type_name` maps these to `"bus"`, `"pci"`, `"block"`, `"char"`,
`"input"`, `"console"`, `"storage"` (the array has `DEV_TYPE_COUNT` slots; the
mouse/console/storage strings are present), with `"unknown"` for out-of-range.
`device_init_struct` zeroes the device, copies the name, sets the type, and
defaults `id.vendor`/`id.device` to `0xFFFF` (match-any).

### `struct char_device` (`char_device.h`)

```c
struct char_device {
    const char *name;
    int64_t (*read) (struct char_device *cd, void *buf, uint64_t size);
    int64_t (*write)(struct char_device *cd, const void *buf, uint64_t size);
    void *priv;
};
```

Char devices live in a **separate** flat table (`device.c`, `DEVICE_MAX_CHAR`
= 16), not in the generic `g_devices` list — they predate the driver core
(`/dev/null`, `/dev/zero`, console) and are enumerated by devfs.

### `struct hw_event` (`hw_event.h`)

```c
enum hw_event_type {
    HW_EVENT_DEVICE_ADDED, HW_EVENT_DEVICE_REMOVED,
    HW_EVENT_DRIVER_BOUND, HW_EVENT_DRIVER_FAILED,
    HW_EVENT_INPUT,
    HW_EVENT_USB_DEVICE_ATTACHED, HW_EVENT_USB_DEVICE_REMOVED,
    HW_EVENT_TYPE_COUNT,
};

#define HW_EVENT_MSG_MAX 48
struct hw_event {
    uint32_t type;                       /* enum hw_event_type    */
    uint32_t seq;                        /* monotonic sequence id */
    uint64_t a, b;                       /* type-specific operands */
    char     message[HW_EVENT_MSG_MAX];
};
```

### `struct hw_diag_summary` (`hw_diag.h`)

```c
struct hw_diag_summary {
    uint32_t pci_functions, devices, block_devices, usb_devices;
    uint32_t irq_active_vectors;
    uint64_t irq_total, hw_events;
};
```

## Key APIs

### Core + registries

```c
void           driver_core_init(void);                  /* "[OK] Driver core online" */
void           device_init_struct(struct device*, const char *name, enum device_type);
void           driver_registry_init(void);
int            driver_register(struct driver*);         /* prepend; -1 on NULL */
int            device_register(struct device*);         /* tail-append; -1 on NULL */
int            driver_probe_all(void);                  /* bind unbound devices, return new bindings */
void           device_dump_all(void);
void           driver_dump_all(void);
int            device_count(void);
struct device *device_at(int index);
struct driver *driver_registry_head(void);
```

`driver_probe_all` skips already-bound devices and, for each, tries every
same-type driver with a non-NULL `probe`, binding the first to return 0. It does
not change lifecycle state or emit events — it is the bulk bind used at boot.

### Lifecycle dispatch (`driver_lifecycle.c`, `device_reset.c`)

```c
void        driver_lifecycle_init(void);                /* "[OK] Driver lifecycle online" */
int         driver_probe  (struct device*);             /* returns enum driver_probe_result */
int         driver_remove (struct device*);
int         driver_suspend(struct device*);
int         driver_resume (struct device*);
int         driver_reset  (struct device*);
void        device_set_state(struct device*, enum device_state);
const char *device_state_name(enum device_state);
```

State machine per entry point:

- **`driver_probe`** — set DISCOVERED; for each matching driver: set MATCHED, call
  `probe`. On 0: bind, set ACTIVE, `device_power_set(D0)`, emit
  `HW_EVENT_DRIVER_BOUND` (operand `a` = device pointer, message = driver name),
  return `DRIVER_PROBE_OK`. If no driver claims it: emit `HW_EVENT_DRIVER_FAILED`
  (message = device name), return `DRIVER_PROBE_NOMATCH`.
- **`driver_remove`** — call `driver->remove` if present; unbind
  (`driver = NULL`); `device_power_set(D3)`; set REMOVED; emit
  `HW_EVENT_DEVICE_REMOVED`. Returns the hook's result (0 if no hook).
- **`driver_suspend`** — call `suspend` if present; `device_power_set(D3)`; set
  SUSPENDED.
- **`driver_resume`** — `device_power_set(D0)` first, then call `resume` if
  present; set ACTIVE. (Order matters: power up before the hook runs.)
- **`driver_reset`** — `device_reset_power_cycle` (D3→D0); call `reset` if present;
  set ACTIVE. A successful reset always leaves the device active again.

All five reject a NULL device (`driver_probe` returns `DRIVER_PROBE_ERROR`; the
others return -1).

### Power + reset

```c
int                     device_power_set(struct device*, enum device_power_state);
enum device_power_state device_power_get(struct device*);    /* D3 for NULL */
const char             *device_power_state_name(enum device_power_state);
int                     device_reset_power_cycle(struct device*);  /* D3 then D0, returns 0 */
```

`device_power_set` simply stores `dev->power_state`. Bus drivers may layer real
register writes on top later; today it is pure accounting.

### Hardware event bus

```c
void     hw_event_bus_init(void);                       /* "[OK] Hardware event bus online" */
void     hw_event_emit(enum hw_event_type, uint64_t a, uint64_t b, const char *message);
uint64_t hw_event_count(void);
int      hw_event_recent(struct hw_event *out, int max);   /* oldest-first window, returns count */
void     hw_event_dump_recent(void);
const char *hw_event_type_name(enum hw_event_type);
```

The bus is a fixed 64-entry ring (`HW_EVENT_RING`) plus a lifetime `g_total`.
`hw_event_emit` is lock-light: it brackets the ring write with
`irq_save_flags_and_disable()` / `irq_restore_flags()` so input drivers can emit
from interrupt or poll context. Each event gets `seq = g_total` and the message
is `strlcpy`'d into the 48-byte buffer. Emits before `hw_event_bus_init` (i.e.
while `g_ready == 0`) are dropped.

`hw_event_recent` copies the newest `max` events oldest-first into the caller's
buffer (`have = min(g_total, 64)`), again under IRQ-save.

### Diagnostics

```c
void hw_diag_collect(struct hw_diag_summary *out);
void hw_diag_dump(void);
```

`hw_diag_collect` snapshots: `pci_device_count()`, `device_count()`,
`block_device_count()`, `usb_device_count()` (a **weak** symbol — resolves to 0
until the USB core links in), and walks IRQ vectors `0x20..0x4F` summing
`irq_count_for_vector()` to get active-vector count and total interrupts, plus
`hw_event_count()`. `hw_diag_dump` prints the snapshot; it backs the userland
`hwinfo` tool.

## Invariants

- **Generic matching is by `device_type` only.** The generic core never inspects
  vendor/class; that is the PCI layer's job. A driver only sees devices whose
  `type` equals its own.
- **Drivers prepend, devices append.** Driver lookup order is reverse
  registration order; device enumeration order (`device_at`) is registration
  order and stable for the life of boot.
- **The state machine is the contract; hooks are optional.** Every lifecycle
  entry point updates state (and, where relevant, power state and events) even
  when the bound driver provides no matching hook.
- **`driver_resume` powers up before calling the hook; `driver_suspend` powers
  down after.** Symmetric so the device is reachable when the resume hook runs and
  still reachable when the suspend hook runs.
- **A successful reset returns the device to ACTIVE.** `driver_reset` sets ACTIVE
  unconditionally after the power cycle + hook.
- **`remove` unbinds and parks at D3.** After `driver_remove`, `dev->driver` is
  NULL and `power_state` is D3; the device stays in the registry (no removal from
  the list).
- **Event emits are atomic w.r.t. the ring.** IRQ-save brackets every ring
  mutation, so a concurrent interrupt cannot interleave a partial event.
- **Events before bus init are dropped, not faulted.** The `g_ready` guard makes
  early `hw_event_emit` calls safe no-ops.
- **`usb_device_count` is weak.** `hw_diag` tolerates the USB core being absent
  by treating the count as 0.

## Failure modes

- **NULL driver/device at register →** `driver_register`/`device_register` return
  -1; nothing is added.
- **NULL device at a lifecycle call →** `driver_probe` returns
  `DRIVER_PROBE_ERROR`; `driver_remove`/`_suspend`/`_resume`/`_reset` return -1.
- **No matching driver in `driver_probe` →** device left at DISCOVERED-then-loop;
  emits `HW_EVENT_DRIVER_FAILED`; returns `DRIVER_PROBE_NOMATCH`.
- **A `probe` returning non-zero →** treated as decline; the loop continues to the
  next driver. (There is no separate ERROR path in the dispatcher; any non-zero
  means "not claimed".)
- **Char-device table full →** `device_register_char` returns `-SYS_ENOMEM` once
  `g_char_count >= DEVICE_MAX_CHAR` (16).
- **Event ring overflow →** by design: the 65th event overwrites the oldest;
  `g_total` keeps the true lifetime count, so `hw_event_count()` stays accurate
  even though only the last 64 are retrievable.
- **Diagnostics with USB absent →** `usb_devices` reported as 0 via the weak
  symbol; no crash.

## Verification

```
make verify-driver-lifecycle    # lifecycle + event bus self-test
make verify-msi                 # capabilities/MSI (PCI side, see pci_deep.md)
make verify-prompt6             # full Prompt 6 chain incl. the above
```

Serial markers and emitters:

| Marker | Emitter |
| --- | --- |
| `[OK] Driver core online` | `driver_core_init` (`driver.c`) |
| `[OK] Driver lifecycle online` | `driver_lifecycle_init` (`driver_lifecycle.c`) |
| `[OK] Hardware event bus online` | `hw_event_bus_init` (`hw_event_bus.c`) |
| `[PASS] driver lifecycle tests` | `driver_tests_run` (`driver_tests.c`) |

`verify-driver-lifecycle` expects `[OK] Driver lifecycle online`, `[OK] Hardware
event bus online`, and `[PASS] driver lifecycle tests`.

What the self-test (`driver_tests.c`) actually asserts, against the *real*
dispatchers:

1. Registers a mock `DEV_TYPE_BUS` driver with all five hooks (each increments a
   call counter).
2. `driver_probe` → expects `DRIVER_PROBE_OK`, state ACTIVE, `driver` bound,
   `mock_probe_calls == 1`.
3. `driver_suspend` → state SUSPENDED, power D3, `mock_suspend_calls == 1`.
4. `driver_resume` → state ACTIVE, power D0, `mock_resume_calls == 1`.
5. `driver_reset` → state ACTIVE, `mock_reset_calls == 1`.
6. `driver_remove` → state REMOVED, `driver == NULL`, `mock_remove_calls == 1`.
7. `hw_event_count()` grew by at least 2 (the bound + removed events).

Only if every assertion holds does it print `[PASS] driver lifecycle tests`;
otherwise it logs a specific `[ERROR]` line and returns. The expanded suite adds
`verify-driver-expanded` to `verify-production-200k`.

## Future expansion

- **Use `DEVICE_STATE_INITIALIZED` / `DEVICE_STATE_FAILED`.** They exist in the
  enum but the dispatchers skip INITIALIZED and never set FAILED; a richer
  state machine (separate init step, explicit failure parking) can adopt them.
- **D1/D2 power states.** Defined but unused; runtime PM could use the
  intermediate states instead of jumping straight to D3.
- **Per-driver power/reset register hooks.** `device_power_set` is accounting
  only; bus drivers can override to poke real PCI PM capability registers or
  controller reset bits.
- **Hot-unplug / dynamic removal.** Devices are never removed from `g_devices`;
  a real hot-unplug path would unlink and free, and `HW_EVENT_DEVICE_REMOVED` is
  already wired for observers.
- **Userland event delivery.** `hw_event_recent` is shaped for a syscall/devfs
  node so userland can stream lifecycle events; today only the kernel log and
  `hwinfo` consume it.
- **Unify the char-device table.** The flat `DEVICE_MAX_CHAR` table predates the
  generic registry; it could be folded into `g_devices` as `DEV_TYPE_CHAR`
  entries for a single enumeration path.
