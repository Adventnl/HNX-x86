/* Kernel assertion failure handler. */
#include "assert.h"
#include "panic.h"
#include "log.h"

void kassert_fail(const char *expr, const char *file, int line) {
    kernel_log_error("assertion failed");
    kernel_log("    expr: ");
    kernel_log_line(expr);
    kernel_log("    file: ");
    kernel_log_line(file);
    kernel_log_hex64("    line: ", (uint64_t)line);
    kernel_panic("assertion failed");
}
