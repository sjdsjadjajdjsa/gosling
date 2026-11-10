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

typedef struct {
    char key[1024], title[256], artist[256], album[256], thumb[512];
    double duration;
    int64_t item;
} track;

enum { F_TITLE, F_ARTIST, F_ALBUM, F_KEY, F_THUMB };

static inline const char *track_field(const track *t, int field) {
    switch (field) {
    case F_TITLE: return t->title;
    case F_ARTIST: return t->artist;
    case F_ALBUM: return t->album;
    case F_KEY: return t->key;
    default: return t->thumb;
    }
}

static inline bool track_local(const track *t) { return t->key[0] == '/'; }

void ui_wake(void);
bool search_track(int32_t i, track *out);
int32_t search_count(void);
void art_draw(const char *url, float x, float y, float size, uint32_t bg, uint32_t fg);
bool art_file(const char *url, char *out, size_t cap);
void art_init(const char *cache);
void scan_start(const char *db, const char *folder, const char *cache);
int32_t scan_busy(void);
int32_t scan_changes(void);
void np_init(void);
void np_update(const track *t, double duration, double position, bool playing);
int32_t np_command(double *value);
bool platform_pick_folder(char *out, size_t cap);
void platform_paths(char *data, char *cache, size_t cap);

#endif
