/* Character-device registry. */
#include "device.h"
#include "char_device.h"
#include "syscall_numbers.h"
#include "string.h"

static struct char_device *g_chars[DEVICE_MAX_CHAR];
static int g_char_count;

void device_init(void) {
    g_char_count = 0;
    for (int i = 0; i < DEVICE_MAX_CHAR; i++) {
        g_chars[i] = NULL;
    }
    device_register_char(char_device_null());
    device_register_char(char_device_zero());
}

int device_register_char(struct char_device *cd) {
    if (!cd || g_char_count >= DEVICE_MAX_CHAR) {
        return -SYS_ENOMEM;
    }
    g_chars[g_char_count++] = cd;
    return 0;
}

struct char_device *device_find_char(const char *name) {
    for (int i = 0; i < g_char_count; i++) {
        if (strcmp(g_chars[i]->name, name) == 0) {
            return g_chars[i];
        }
    }
    return NULL;
}

int device_char_count(void) {
    return g_char_count;
}

struct char_device *device_char_at(int index) {
    if (index < 0 || index >= g_char_count) {
        return NULL;
    }
    return g_chars[index];
}
