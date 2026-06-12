/* Framebuffer text console (independent of UEFI). */
#ifndef MYOS_FRAMEBUFFER_CONSOLE_H
#define MYOS_FRAMEBUFFER_CONSOLE_H

#include "types.h"
#include "boot_info.h"

/* Initialize from the bootloader-provided framebuffer description. */
void fbcon_init(const struct boot_framebuffer *fb);

/* Whether a usable framebuffer is available. */
bool fbcon_ready(void);

/* True if the pixel format was unrecognized and a direct-0x00RRGGBB fallback
 * is in use (caller should warn). */
bool fbcon_format_fallback(void);

/* Pack 8-bit R/G/B into a pixel value for the active framebuffer format. */
u32 fbcon_pack_color(u8 r, u8 g, u8 b);

/* Set foreground/background colors (arguments are 0x00RRGGBB). */
void fbcon_set_color(u32 fg, u32 bg);

/* Fill the whole screen with `color` and home the cursor. */
void fbcon_clear(u32 color);

/* Plot a single pixel. */
void fbcon_putpixel(u32 x, u32 y, u32 color);

/* Character / string output with newline handling. */
void fbcon_putc(char c);
void fbcon_puts(const char *s);

/* Numeric helpers. */
void fbcon_put_hex(u64 value);
void fbcon_put_dec(u64 value);

#endif /* MYOS_FRAMEBUFFER_CONSOLE_H */
