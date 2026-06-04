#include "Support.h"

//===----------------------------------------------------------------------===//
// AetherKernel — minimal libc shims for the Embedded _Concurrency runtime.
//
// The concurrency runtime's error/reporting paths reference a handful of libc
// symbols. None are on the hot path (task create/run/sleep); they fire only on
// fatalError/abort. We provide just enough: real string/mem ops, and UART-backed
// puts/vprintf/abort so a runtime fault is visible on serial instead of a silent
// hang. Formatting (vprintf/vsnprintf) is intentionally minimal — it emits the
// format string literally (no specifier expansion), which is adequate for a
// bare-metal crash channel.
//===----------------------------------------------------------------------===//

typedef unsigned long size_t;

#define UART0_DR   0xFE201000UL
#define UART0_FR   0xFE201018UL
#define UART0_FR_TXFF (1U << 5)

static void shim_putc(char c) {
    while (mmio_read32(UART0_FR) & UART0_FR_TXFF) {
        nop();
    }
    mmio_write32(UART0_DR, (unsigned int)(unsigned char)c);
}

static void shim_puts_raw(const char *s) {
    while (*s != '\0') {
        if (*s == '\n') {
            shim_putc('\r');
        }
        shim_putc(*s);
        s++;
    }
}

// mem* — clang lowers loop idioms (and the runtime's own copies/fills) to these.
// libc_shims.c is compiled with -fno-builtin so they don't self-optimize into
// recursive calls.
void *memset(void *dst, int c, size_t n) {
    unsigned char *p = (unsigned char *)dst;
    for (size_t i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dst;
}

size_t strlen(const char *s) {
    const char *p = s;
    while (*p != '\0') {
        p++;
    }
    return (size_t)(p - s);
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    for (; i < n; i++) {
        dst[i] = '\0';
    }
    return dst;
}

// C11 memset_s: like memset but returns an errno_t (0 on success).
int memset_s(void *dst, size_t dstsz, int c, size_t n) {
    (void)dstsz;
    unsigned char *p = (unsigned char *)dst;
    for (size_t i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return 0;
}

// puts: write string + newline to UART. Returns non-negative on success.
int puts(const char *s) {
    shim_puts_raw(s);
    shim_putc('\r');
    shim_putc('\n');
    return 0;
}

// vprintf/vsnprintf: minimal crash-channel formatting — emit the format string
// literally (no %-specifier expansion). Args are ignored.
int vprintf(const char *fmt, __builtin_va_list ap) {
    (void)ap;
    shim_puts_raw(fmt);
    return (int)strlen(fmt);
}

int vsnprintf(char *buf, size_t size, const char *fmt, __builtin_va_list ap) {
    (void)ap;
    if (size == 0) {
        return 0;
    }
    size_t i = 0;
    for (; fmt[i] != '\0' && i < size - 1; i++) {
        buf[i] = fmt[i];
    }
    buf[i] = '\0';
    return (int)i;
}

// abort: report on UART and park the core. Never returns.
void abort(void) {
    shim_puts_raw("abort()\r\n");
    for (;;) {
        wait_for_interrupt();
    }
}
