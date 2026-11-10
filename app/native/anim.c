#include <math.h>
#include "native.h"

double ui_time(void);

enum { SLOTS = 192 };

static struct { int32_t id; float v; unsigned seen; } slots[SLOTS];
static unsigned frame;
static double last;
static float dt;
static bool moving;

void ui_anim_begin(void) {
    double now = ui_time();
    double d = now - last;
    dt = (float)(d > 1.0 / 30 ? 1.0 / 60 : d);
    last = now;
    frame++;
    moving = false;
}

float ui_fade(int32_t id, bool on) {
    int free_slot = -1, oldest = 0;
    for (int i = 0; i < SLOTS; i++) {
        if (slots[i].seen && slots[i].id == id) {
            float target = on ? 1.0f : 0.0f;
            float v = slots[i].v + (target - slots[i].v) * (1.0f - expf(-dt * 16.0f));
            if (fabsf(target - v) < 0.02f) v = target;
            else moving = true;
            slots[i].v = v;
            slots[i].seen = frame;
            return v;
        }
        if (!slots[i].seen && free_slot < 0) free_slot = i;
        if (slots[i].seen < slots[oldest].seen) oldest = i;
    }
    if (!on) return 0;
    int i = free_slot >= 0 ? free_slot : oldest;
    slots[i].id = id;
    slots[i].v = 0;
    slots[i].seen = frame;
    moving = true;
    return 0.15f;
}

bool ui_animating(void) { return moving; }

uint32_t ui_mix(uint32_t a, uint32_t b, float t) {
    if (t <= 0) return a;
    if (t >= 1) return b;
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        float x = (float)((a >> s) & 255), y = (float)((b >> s) & 255);
        out |= (uint32_t)(x + (y - x) * t + 0.5f) << s;
    }
    return out;
}
