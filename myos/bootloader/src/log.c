/* Stage 8: UEFI screen / log output (pre-ExitBootServices only). */
#include "bootloader.h"

static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *con = NULL;

static void out16(const CHAR16 *s) {
    if (con) {
        con->OutputString(con, (CHAR16 *)s);
    }
}

void log_init(void) {
    con = gST->ConOut;
    if (con) {
        con->Reset(con, FALSE);
        con->ClearScreen(con);
    }
}

void log_cstr(const char *s) {
    CHAR16 buf[160];
    UINTN j = 0;
    while (*s) {
        char c = *s++;
        if (j >= 156) {
            buf[j] = 0;
            out16(buf);
            j = 0;
        }
        if (c == '\n') {
            buf[j++] = (CHAR16)'\r';
            buf[j++] = (CHAR16)'\n';
        } else {
            buf[j++] = (CHAR16)(unsigned char)c;
        }
    }
    buf[j] = 0;
    out16(buf);
}

void log_hex(myos_u64 value) {
    static const char digits[] = "0123456789ABCDEF";
    char b[19];
    b[0] = '0';
    b[1] = 'x';
    for (int i = 0; i < 16; i++) {
        b[2 + i] = digits[(value >> ((15 - i) * 4)) & 0xF];
    }
    b[18] = 0;
    log_cstr(b);
}

void log_dec(myos_u64 value) {
    char b[21];
    int i = 20;
    b[i] = 0;
    if (value == 0) {
        b[--i] = '0';
    }
    while (value && i > 0) {
        b[--i] = (char)('0' + (value % 10));
        value /= 10;
    }
    log_cstr(&b[i]);
}

void log_ok(const char *msg) {
    log_cstr("[OK] ");
    log_cstr(msg);
    log_cstr("\n");
}

void log_err(const char *stage, EFI_STATUS status) {
    log_cstr("[ERROR] ");
    log_cstr(stage);
    log_cstr("\nstatus: ");
    log_hex((myos_u64)status);
    log_cstr("\n");
}
