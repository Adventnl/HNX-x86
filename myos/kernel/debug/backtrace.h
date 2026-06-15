/* Stack backtrace via frame-pointer walking.
 *
 * The kernel is compiled at -O0 so every function maintains the classic
 * rbp chain: [rbp] -> saved rbp, [rbp+8] -> return address. backtrace_capture
 * walks that chain into a caller buffer; backtrace_print resolves each address
 * through the symbol registry and logs "frame N: name+0xNN".
 */
#ifndef MYOS_DEBUG_BACKTRACE_H
#define MYOS_DEBUG_BACKTRACE_H

#include "types.h"

#define BACKTRACE_MAX_FRAMES 32

/* Capture up to `max` return addresses starting from the caller's frame.
 * Returns the number captured. If `start_rbp` is 0 the current rbp is used. */
size_t backtrace_capture(uint64_t *frames, size_t max, uint64_t start_rbp);

/* Capture and log a symbolized backtrace. */
void backtrace_print(const char *label);
void backtrace_print_from(uint64_t rbp, const char *label);

#endif /* MYOS_DEBUG_BACKTRACE_H */
