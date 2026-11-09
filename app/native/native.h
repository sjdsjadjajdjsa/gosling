#ifndef GOSLING_NATIVE_H
#define GOSLING_NATIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct { const uint8_t *data; int64_t len; } gs_str;

static inline const char *gs_cstr(gs_str s, char *buf, size_t cap) {
    size_t n = (size_t)s.len < cap - 1 ? (size_t)s.len : cap - 1;
    memcpy(buf, s.data, n);
    buf[n] = 0;
    return buf;
}

static inline int32_t gs_copy(char *out, size_t cap, const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (n > cap) {
        n = cap;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    }
    if (n) memcpy(out, s, n);
    return (int32_t)n;
}

void ui_wake(void);

#endif
