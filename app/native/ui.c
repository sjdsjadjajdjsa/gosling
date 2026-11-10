#include <raylib.h>
#include <GLFW/glfw3.h>
#include "native.h"

static const unsigned char inter[] = {
#embed "../fonts/Inter-Medium.ttf"
};

enum { FONTS = 8 };
static struct { int px; Font font; } fonts[FONTS];
static int nfonts, nextfont;
static float dpi = 1;
static int glyphs[1024];
static int nglyphs;
static int cursor = -1;
static GLFWmousebuttonfun raylib_mouse;
static bool clicked, unclicked, pressed, released;
static int keys[16];
static int nkeys;

static void on_mouse(GLFWwindow *window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) clicked = true;
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) unclicked = true;
    if (raylib_mouse) raylib_mouse(window, button, action, mods);
}

static void add_range(int lo, int hi) {
    for (int c = lo; c <= hi && nglyphs < 1024; c++) glyphs[nglyphs++] = c;
}

static Font font_at(float size) {
    int px = (int)(size * dpi + 0.5f);
    for (int i = 0; i < nfonts; i++)
        if (fonts[i].px == px) return fonts[i].font;
    if (!nglyphs) {
        add_range(0x20, 0x7E);
        add_range(0xA0, 0x24F);
        add_range(0x370, 0x3FF);
        add_range(0x400, 0x4FF);
        add_range(0x2010, 0x2027);
        add_range(0x2030, 0x203A);
        add_range(0x20AC, 0x20AC);
        add_range(0x2122, 0x2122);
    }
    Font f = LoadFontFromMemory(".ttf", inter, sizeof inter, px, glyphs, nglyphs);
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    for (int i = 0; i < f.glyphCount; i++) {
        UnloadImage(f.glyphs[i].image);
        f.glyphs[i].image = (Image){ 0 };
    }
    int slot = nfonts < FONTS ? nfonts++ : nextfont;
    if (nfonts == FONTS && slot == nextfont && fonts[slot].px) UnloadFont(fonts[slot].font);
    nextfont = (slot + 1) % FONTS;
    fonts[slot].px = px;
    fonts[slot].font = f;
    return f;
}

static Color rgba(uint32_t c) {
    return (Color){ (unsigned char)(c >> 24), (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c };
}

void ui_open(gs_str title, int32_t w, int32_t h, int32_t min_w, int32_t min_h) {
    char buf[256];
    SetTraceLogLevel(LOG_ERROR);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_VSYNC_HINT);
    InitWindow(w, h, gs_cstr(title, buf, sizeof buf));
    SetWindowMinSize(min_w, min_h);
    SetExitKey(KEY_NULL);
    raylib_mouse = glfwSetMouseButtonCallback(glfwGetCurrentContext(), on_mouse);
    dpi = GetWindowScaleDPI().x;
}

bool ui_running(void) { return !WindowShouldClose(); }

void ui_wait(double timeout) {
    if (timeout < 0) glfwWaitEvents();
    else if (timeout > 0) glfwWaitEventsTimeout(timeout);
}

void ui_wake(void) { glfwPostEmptyEvent(); }

bool ui_visible(void) { return !IsWindowMinimized() && !IsWindowHidden(); }

double ui_time(void) { return GetTime(); }

void ui_input(void) {
    pressed = clicked;
    released = unclicked;
    clicked = unclicked = false;
    nkeys = 0;
    for (int k; nkeys < 16 && (k = GetKeyPressed());) keys[nkeys++] = k;
}

void ui_frame(uint32_t bg) {
    BeginDrawing();
    ClearBackground(rgba(bg));
}

void ui_present(void) { EndDrawing(); }

int32_t ui_width(void) { return GetScreenWidth(); }
int32_t ui_height(void) { return GetScreenHeight(); }

void ui_rect(float x, float y, float w, float h, float r, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    Rectangle rec = { x, y, w, h };
    float m = w < h ? w : h;
    if (r <= 0) DrawRectangleRec(rec, rgba(color));
    else DrawRectangleRounded(rec, r * 2 >= m ? 1.0f : r * 2 / m, 12, rgba(color));
}

void ui_ring(float x, float y, float inner, float outer, float start, float end, uint32_t color) {
    DrawRing((Vector2){ x, y }, inner, outer, start, end, 32, rgba(color));
}

void ui_circle(float x, float y, float r, uint32_t color) { DrawCircleV((Vector2){ x, y }, r, rgba(color)); }

void ui_triangle(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color) {
    DrawTriangle((Vector2){ x1, y1 }, (Vector2){ x2, y2 }, (Vector2){ x3, y3 }, rgba(color));
}

void ui_text(gs_str s, float x, float y, float size, uint32_t color) {
    char buf[1024];
    DrawTextEx(font_at(size), gs_cstr(s, buf, sizeof buf), (Vector2){ x, y }, size, 0, rgba(color));
}

float ui_text_width(gs_str s, float size) {
    char buf[1024];
    return MeasureTextEx(font_at(size), gs_cstr(s, buf, sizeof buf), size, 0).x;
}

void ui_text_fit(gs_str s, float x, float y, float size, uint32_t color, float max_w) {
    char buf[1024];
    Font f = font_at(size);
    gs_cstr(s, buf, sizeof buf - 4);
    if (MeasureTextEx(f, buf, size, 0).x > max_w) {
        size_t n = strlen(buf);
        while (n > 0) {
            n--;
            while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80) n--;
            while (n > 0 && buf[n - 1] == ' ') n--;
            memcpy(buf + n, "\xE2\x80\xA6", 4);
            if (MeasureTextEx(f, buf, size, 0).x <= max_w) break;
        }
    }
    DrawTextEx(f, buf, (Vector2){ x, y }, size, 0, rgba(color));
}

void ui_clip(float x, float y, float w, float h) { BeginScissorMode((int)x, (int)y, (int)w, (int)h); }
void ui_unclip(void) { EndScissorMode(); }

float ui_mouse_x(void) { return GetMouseX(); }
float ui_mouse_y(void) { return GetMouseY(); }
bool ui_mouse_pressed(void) { return pressed; }
bool ui_mouse_down(void) { return IsMouseButtonDown(MOUSE_BUTTON_LEFT); }
bool ui_mouse_released(void) { return released; }
float ui_wheel(void) { return GetMouseWheelMove(); }
int32_t ui_char(void) { return GetCharPressed(); }
bool ui_key(int32_t key) {
    for (int i = 0; i < nkeys; i++)
        if (keys[i] == key) return true;
    return IsKeyPressedRepeat(key);
}
bool ui_command(void) {
    return IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER) || IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
}

int32_t ui_paste(char out[1024]) { return gs_copy(out, 1024, GetClipboardText()); }

int32_t ui_dropped(char out[1024]) {
    if (!IsFileDropped()) return 0;
    FilePathList files = LoadDroppedFiles();
    int32_t n = files.count ? gs_copy(out, 1024, files.paths[0]) : 0;
    UnloadDroppedFiles(files);
    return n;
}

void ui_cursor(int32_t kind) {
    if (kind == cursor) return;
    cursor = kind;
    SetMouseCursor(kind);
}

void ui_close(void) {
    for (int i = 0; i < nfonts; i++) UnloadFont(fonts[i].font);
    nfonts = 0;
    CloseWindow();
}
