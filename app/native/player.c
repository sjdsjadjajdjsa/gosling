#include <mpv/client.h>
#include <stdio.h>
#include <stdlib.h>
#include "native.h"

static mpv_handle *mpv;
static struct {
    bool paused, idle, loading, failed, ended;
    double duration;
    char title[512];
} st = { .idle = true };

enum { P_PAUSE = 1, P_DURATION, P_TITLE, P_IDLE };

static void wake(void *data) {
    (void)data;
    ui_wake();
}

static void option(const char *name, const char *value) { mpv_set_option_string(mpv, name, value); }

bool player_open(void) {
    mpv = mpv_create();
    if (!mpv) return false;
    option("vid", "no");
    option("video", "no");
    option("audio-display", "no");
    option("idle", "yes");
    option("ytdl", "yes");
    option("ytdl-format", "bestaudio/best");
    option("gapless-audio", "weak");
    option("osc", "no");
    option("input-default-bindings", "no");
    option("load-stats-overlay", "no");
    option("load-console", "no");
    option("load-osd-console", "no");
    option("load-select", "no");
    option("load-positioning", "no");
    option("load-commands", "no");
    option("load-context-menu", "no");
    option("load-auto-profiles", "no");
    const char *extra = getenv("GOSLING_MPV_OPTIONS");
    if (extra) {
        char buf[1024];
        snprintf(buf, sizeof buf, "%s", extra);
        for (char *save, *pair = strtok_r(buf, ",", &save); pair; pair = strtok_r(NULL, ",", &save)) {
            char *eq = strchr(pair, '=');
            if (!eq) continue;
            *eq = 0;
            option(pair, eq + 1);
        }
    }
    if (mpv_initialize(mpv) < 0) return false;
    mpv_observe_property(mpv, P_PAUSE, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv, P_DURATION, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, P_TITLE, "media-title", MPV_FORMAT_STRING);
    mpv_observe_property(mpv, P_IDLE, "idle-active", MPV_FORMAT_FLAG);
    mpv_set_wakeup_callback(mpv, wake, NULL);
    return true;
}

static void changed(mpv_event_property *p, uint64_t id) {
    if (p->format == MPV_FORMAT_NONE) {
        if (id == P_DURATION) st.duration = 0;
        if (id == P_TITLE) st.title[0] = 0;
        return;
    }
    switch (id) {
    case P_PAUSE: st.paused = *(int *)p->data; break;
    case P_IDLE: st.idle = *(int *)p->data; break;
    case P_DURATION: st.duration = *(double *)p->data; break;
    case P_TITLE: snprintf(st.title, sizeof st.title, "%s", *(char **)p->data); break;
    }
}

void player_update(void) {
    for (;;) {
        mpv_event *e = mpv_wait_event(mpv, 0);
        if (e->event_id == MPV_EVENT_NONE) break;
        switch (e->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE: changed(e->data, e->reply_userdata); break;
        case MPV_EVENT_PLAYBACK_RESTART: st.loading = false; break;
        case MPV_EVENT_END_FILE: {
            mpv_event_end_file *end = e->data;
            st.loading = false;
            if (end->reason == MPV_END_FILE_REASON_ERROR) st.failed = true;
            if (end->reason == MPV_END_FILE_REASON_EOF) st.ended = true;
            break;
        }
        default: break;
        }
    }
}

void player_load(gs_str url) {
    char buf[2048];
    const char *cmd[] = { "loadfile", gs_cstr(url, buf, sizeof buf), NULL };
    st.loading = true;
    st.failed = false;
    st.ended = false;
    mpv_command_async(mpv, 0, cmd);
    int no = 0;
    mpv_set_property_async(mpv, 0, "pause", MPV_FORMAT_FLAG, &no);
}

void player_toggle(void) {
    int pause = !st.paused;
    mpv_set_property_async(mpv, 0, "pause", MPV_FORMAT_FLAG, &pause);
}

void player_seek(double secs) {
    char buf[64];
    snprintf(buf, sizeof buf, "%.3f", secs < 0 ? 0 : secs);
    const char *cmd[] = { "seek", buf, "absolute", NULL };
    mpv_command_async(mpv, 0, cmd);
}

void player_volume(double volume) { mpv_set_property_async(mpv, 0, "volume", MPV_FORMAT_DOUBLE, &volume); }

double player_time(void) {
    double t = 0;
    if (!st.idle) mpv_get_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &t);
    return t;
}

double player_duration(void) { return st.duration; }
bool player_paused(void) { return st.paused; }
bool player_idle(void) { return st.idle; }
bool player_loading(void) { return st.loading; }
bool player_failed(void) { return st.failed; }

bool player_ended(void) {
    bool e = st.ended;
    st.ended = false;
    return e;
}

int32_t player_title(char out[512]) { return gs_copy(out, 512, st.title); }

void player_close(void) {
    if (mpv) mpv_terminate_destroy(mpv);
    mpv = NULL;
}
