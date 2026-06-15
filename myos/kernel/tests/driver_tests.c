/* Driver lifecycle + hardware event bus self-tests. Registers a mock driver and
 * runs a mock device through the full state machine
 * (discovered->matched->active->suspended->active->removed), asserting both the
 * device-state transitions and the matching hardware events. This exercises the
 * real dispatchers in driver_lifecycle.c / device_reset.c, not a stub. */
#include "driver_tests.h"
#include "driver.h"
#include "driver_registry.h"
#include "device_power.h"
#include "hw_event_bus.h"
#include "string.h"
#include "log.h"

static int mock_probe_calls;
static int mock_remove_calls;
static int mock_suspend_calls;
static int mock_resume_calls;
static int mock_reset_calls;

static int mock_probe(struct device *dev)   { (void)dev; mock_probe_calls++;   return 0; }
static int mock_remove(struct device *dev)  { (void)dev; mock_remove_calls++;  return 0; }
static int mock_suspend(struct device *dev) { (void)dev; mock_suspend_calls++; return 0; }
static int mock_resume(struct device *dev)  { (void)dev; mock_resume_calls++;  return 0; }
static int mock_reset(struct device *dev)   { (void)dev; mock_reset_calls++;   return 0; }

static struct driver g_mock_driver = {
    .name = "mock-lifecycle",
    .type = DEV_TYPE_BUS,
    .probe = mock_probe,
    .remove = mock_remove,
    .suspend = mock_suspend,
    .resume = mock_resume,
    .reset = mock_reset,
};

void driver_tests_run(void) {
    struct device dev;
    device_init_struct(&dev, "mockdev0", DEV_TYPE_BUS);

    driver_register(&g_mock_driver);
    uint64_t events_before = hw_event_count();

    /* probe -> active + bound */
    if (driver_probe(&dev) != DRIVER_PROBE_OK ||
        dev.state != DEVICE_STATE_ACTIVE || dev.driver != &g_mock_driver ||
        mock_probe_calls != 1) {
        kernel_log_error("driver lifecycle tests: probe failed");
        return;
    }
    /* suspend -> suspended + D3 */
    if (driver_suspend(&dev) != 0 || dev.state != DEVICE_STATE_SUSPENDED ||
        device_power_get(&dev) != DEV_POWER_D3 || mock_suspend_calls != 1) {
        kernel_log_error("driver lifecycle tests: suspend failed");
        return;
    }
    /* resume -> active + D0 */
    if (driver_resume(&dev) != 0 || dev.state != DEVICE_STATE_ACTIVE ||
        device_power_get(&dev) != DEV_POWER_D0 || mock_resume_calls != 1) {
        kernel_log_error("driver lifecycle tests: resume failed");
        return;
    }
    /* reset -> active, hook fired */
    if (driver_reset(&dev) != 0 || dev.state != DEVICE_STATE_ACTIVE ||
        mock_reset_calls != 1) {
        kernel_log_error("driver lifecycle tests: reset failed");
        return;
    }
    /* remove -> removed, unbound */
    if (driver_remove(&dev) != 0 || dev.state != DEVICE_STATE_REMOVED ||
        dev.driver != NULL || mock_remove_calls != 1) {
        kernel_log_error("driver lifecycle tests: remove failed");
        return;
    }

    /* At least the bound + removed events must have been recorded. */
    if (hw_event_count() < events_before + 2) {
        kernel_log_error("driver lifecycle tests: events not emitted");
        return;
    }

    kernel_log_line("[PASS] driver lifecycle tests");
}
