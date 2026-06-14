/* Character-device dispatch + the null/zero pseudo-devices. */
#include "char_device.h"
#include "syscall_numbers.h"
#include "string.h"

int64_t char_device_read(struct char_device *cd, void *buf, uint64_t size) {
    if (!cd || !cd->read) {
        return -SYS_EINVAL;
    }
    return cd->read(cd, buf, size);
}

int64_t char_device_write(struct char_device *cd, const void *buf, uint64_t size) {
    if (!cd || !cd->write) {
        return -SYS_EINVAL;
    }
    return cd->write(cd, buf, size);
}

/* /dev/null: reads see EOF, writes are discarded. */
static int64_t null_read(struct char_device *cd, void *buf, uint64_t size) {
    (void)cd; (void)buf; (void)size;
    return 0;
}
static int64_t null_write(struct char_device *cd, const void *buf, uint64_t size) {
    (void)cd; (void)buf;
    return (int64_t)size;
}
static struct char_device g_null = { "null", null_read, null_write, NULL };

/* /dev/zero: reads yield zeroes, writes are discarded. */
static int64_t zero_read(struct char_device *cd, void *buf, uint64_t size) {
    (void)cd;
    memset(buf, 0, size);
    return (int64_t)size;
}
static int64_t zero_write(struct char_device *cd, const void *buf, uint64_t size) {
    (void)cd; (void)buf;
    return (int64_t)size;
}
static struct char_device g_zero = { "zero", zero_read, zero_write, NULL };

struct char_device *char_device_null(void) { return &g_null; }
struct char_device *char_device_zero(void) { return &g_zero; }
