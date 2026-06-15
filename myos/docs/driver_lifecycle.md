# Driver Lifecycle + Hardware Event Bus

Prompt 6 extends the driver core (`kernel/driver/`) with an explicit device
lifecycle and adds a hardware event bus + power/reset hooks under `kernel/hw/`.

## Device lifecycle

`struct device` gains a `state` and a `power_state`. States:

```
discovered → matched → initialized → active → suspended → failed → removed
```

`struct driver` gains the hook set `probe / remove / suspend / resume / reset`.
Generic dispatchers in `driver_lifecycle.c` update the device state, invoke the
bound (or matching) driver hook, and emit a hardware event:

| Function | Effect |
|----------|--------|
| `driver_probe` | match a driver, call `probe`, → active + `HW_EVENT_DRIVER_BOUND` |
| `driver_suspend` | call `suspend`, → suspended + power D3 |
| `driver_resume` | power D0, call `resume`, → active |
| `driver_reset` | power-cycle D3→D0, call `reset`, → active |
| `driver_remove` | call `remove`, unbind, → removed + `HW_EVENT_DEVICE_REMOVED` |

Hooks are optional — the state machine is the contract, the hook is the side
effect. Marker: `[OK] Driver lifecycle online`.

## Power states

`kernel/hw/power/device_power.c` tracks ACPI-style D-states (`D0`..`D3`);
`device_reset.c` implements `driver_reset` on top of a `D3→D0` power cycle.
Userland `powerinfo` reports each device's power state.

## Hardware event bus

`kernel/hw/events/` is a fixed ring (64 entries) plus a running total. Drivers
emit on lifecycle/input changes:

```c
enum hw_event_type {
    HW_EVENT_DEVICE_ADDED, HW_EVENT_DEVICE_REMOVED,
    HW_EVENT_DRIVER_BOUND, HW_EVENT_DRIVER_FAILED,
    HW_EVENT_INPUT,
    HW_EVENT_USB_DEVICE_ATTACHED, HW_EVENT_USB_DEVICE_REMOVED,
};
void hw_event_emit(enum hw_event_type, uint64_t a, uint64_t b, const char *msg);
uint64_t hw_event_count(void);
```

The ring write is IRQ-safe so input drivers can emit from interrupt or poll
context. Marker: `[OK] Hardware event bus online`.

## Diagnostics

`kernel/hw/diagnostics/hw_diag.c` collects a single snapshot (PCI functions,
devices, block devices, USB devices, active IRQ vectors + total, hardware
events) consumed by the kernel log and the userland `hwinfo` tool.

## Verification

`driver_tests.c` registers a mock driver and runs a mock device through the full
state machine, asserting both the state transitions and the emitted events:

```
[OK] Driver lifecycle online
[OK] Hardware event bus online
[PASS] driver lifecycle tests
```

`make verify-driver-lifecycle` checks these.
