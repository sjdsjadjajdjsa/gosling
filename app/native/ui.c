#include <stdint.h>
#include <string.h>
#include <raylib.h>

typedef struct { const uint8_t *data; int64_t len; } ui_str;

static const unsigned char inter[] = {
#embed "../fonts/Inter-Medium.ttf"
};

enum { FONTS = 8 };
static struct { int px; Font font; } fonts[FONTS];
static int nfonts, nextfont;
static float dpi = 1;

static Font font_at(float size) {
    int px = (int)(size * dpi + 0.5f);
    for (int i = 0; i < nfonts; i++)
        if (fonts[i].px == px) return fonts[i].font;
    Font f = LoadFontFromMemory(".ttf", inter, sizeof inter, px, NULL, 0);
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    int slot = nfonts < FONTS ? nfonts++ : nextfont;
    if (slot == nextfont && nfonts == FONTS && fonts[slot].px) UnloadFont(fonts[slot].font);
    nextfont = (slot + 1) % FONTS;
    fonts[slot].px = px;
    fonts[slot].font = f;
    return f;
}

static const char *cstr(ui_str s, char *buf, size_t cap) {
    size_t n = (size_t)s.len < cap - 1 ? (size_t)s.len : cap - 1;
    memcpy(buf, s.data, n);
    buf[n] = 0;
    return buf;
}

static Color rgba(uint32_t c) {
    return (Color){ (unsigned char)(c >> 24), (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c };
}

void ui_open(ui_str title, int32_t w, int32_t h, int32_t min_w, int32_t min_h) {
    char buf[256];
    SetTraceLogLevel(LOG_ERROR);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_VSYNC_HINT);
    InitWindow(w, h, cstr(title, buf, sizeof buf));
    SetWindowMinSize(min_w, min_h);
    SetExitKey(KEY_NULL);
    EnableEventWaiting();
    dpi = GetWindowScaleDPI().x;
}

bool ui_running(void) { return !WindowShouldClose(); }

void ui_frame(uint32_t bg) {
    BeginDrawing();
    ClearBackground(rgba(bg));
}

void ui_present(void) { EndDrawing(); }

int32_t ui_width(void) { return GetScreenWidth(); }
int32_t ui_height(void) { return GetScreenHeight(); }

void ui_text(ui_str s, float x, float y, float size, uint32_t color) {
    char buf[1024];
    DrawTextEx(font_at(size), cstr(s, buf, sizeof buf), (Vector2){ x, y }, size, 0, rgba(color));
}

float ui_text_width(ui_str s, float size) {
    char buf[1024];
    return MeasureTextEx(font_at(size), cstr(s, buf, sizeof buf), size, 0).x;
}

void ui_close(void) {
    for (int i = 0; i < nfonts; i++) UnloadFont(fonts[i].font);
    nfonts = 0;
    CloseWindow();
}
