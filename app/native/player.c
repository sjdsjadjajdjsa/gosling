#include <mpv/client.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "native.h"

static mpv_handle *mpv;
static char last_url[2048];
static bool retried, cookies, waiting;
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

static bool exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool firefox_has_cookies(const char *profiles) {
    DIR *d = opendir(profiles);
    if (!d) return false;
    bool found = false;
    char p[1400];
    for (struct dirent *e; !found && (e = readdir(d));) {
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s/cookies.sqlite", profiles, e->d_name);
        found = exists(p);
    }
    closedir(d);
    return found;
}

static const char *browser(void) {
    const char *forced = getenv("GOSLING_COOKIES_BROWSER");
    if (forced && *forced) return forced;
    const char *home = getenv("HOME");
    if (!home) return NULL;
#ifdef __APPLE__
    static const char *places[][2] = {
        { "firefox", "Library/Application Support/Firefox/Profiles" },
        { "chrome", "Library/Application Support/Google/Chrome" },
        { "brave", "Library/Application Support/BraveSoftware/Brave-Browser" },
        { "edge", "Library/Application Support/Microsoft Edge" },
        { "chromium", "Library/Application Support/Chromium" },
        { "safari", "Library/Containers/com.apple.Safari/Data/Library/Cookies" },
    };
#else
    static const char *places[][2] = {
        { "firefox", ".mozilla/firefox" },
        { "firefox", "snap/firefox/common/.mozilla/firefox" },
        { "chrome", ".config/google-chrome" },
        { "chromium", ".config/chromium" },
        { "brave", ".config/BraveSoftware/Brave-Browser" },
        { "edge", ".config/microsoft-edge" },
    };
#endif
    char p[1200];
    for (size_t i = 0; i < sizeof places / sizeof *places; i++) {
        snprintf(p, sizeof p, "%s/%s", home, places[i][1]);
        if (!exists(p)) continue;
        if (!strcmp(places[i][0], "firefox") && !firefox_has_cookies(p)) continue;
        return places[i][0];
    }
    return NULL;
}

static void load(const char *url) {
    const char *cmd[] = { "loadfile", url, NULL };
    st.loading = true;
    st.failed = false;
    st.ended = false;
    mpv_command_async(mpv, 0, cmd);
    int no = 0;
    mpv_set_property_async(mpv, 0, "pause", MPV_FORMAT_FLAG, &no);
}

static bool retry_with_cookies(void) {
    if (retried || cookies || !strstr(last_url, "youtube.com/")) return false;
    const char *b = browser();
    if (!b) return false;
    char raw[128];
    snprintf(raw, sizeof raw, "cookies-from-browser=%s", b);
    mpv_set_property_string(mpv, "ytdl-raw-options", raw);
    retried = cookies = true;
    load(last_url);
    return true;
}

static double volume_wanted = 100;

static bool start(void) {
    if (mpv) return true;
    mpv = mpv_create();
    if (!mpv) return false;
    option("vid", "no");
    option("video", "no");
    option("audio-display", "no");
    option("idle", "yes");
    option("ytdl", "yes");
    option("ytdl-format", "bestaudio/best");
    char opts[1200];
    snprintf(opts, sizeof opts, "ytdl_hook-ytdl_path=%s", ytdlp_path());
    option("script-opts", opts);
    option("gapless-audio", "weak");
    option("cache", "yes");
    option("demuxer-max-bytes", "4MiB");
    option("demuxer-max-back-bytes", "1MiB");
    option("prefetch-playlist", "no");
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
    if (mpv_initialize(mpv) < 0) {
        mpv_terminate_destroy(mpv);
        mpv = NULL;
        return false;
    }
    mpv_observe_property(mpv, P_PAUSE, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv, P_DURATION, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, P_TITLE, "media-title", MPV_FORMAT_STRING);
    mpv_observe_property(mpv, P_IDLE, "idle-active", MPV_FORMAT_FLAG);
    mpv_set_wakeup_callback(mpv, wake, NULL);
    mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &volume_wanted);
    return true;
}

bool player_open(void) {
    st.idle = true;
    ytdlp_init();
    return true;
}

static bool remote(const char *url) { return !strncmp(url, "http://", 7) || !strncmp(url, "https://", 8); }

static void go(void) {
    if (!start()) {
        st.loading = false;
        st.failed = true;
        return;
    }
    load(last_url);
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
    if (waiting) {
        int s = ytdlp_state();
        if (s == 2) {
            waiting = false;
            go();
        } else if (s == 3) {
            waiting = false;
            st.loading = false;
            st.failed = true;
        }
    }
    if (!mpv) return;
    for (;;) {
        mpv_event *e = mpv_wait_event(mpv, 0);
        if (e->event_id == MPV_EVENT_NONE) break;
        switch (e->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE: changed(e->data, e->reply_userdata); break;
        case MPV_EVENT_PLAYBACK_RESTART: st.loading = false; break;
        case MPV_EVENT_END_FILE: {
            mpv_event_end_file *end = e->data;
            if (end->reason == MPV_END_FILE_REASON_ERROR && retry_with_cookies()) break;
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
    gs_cstr(url, last_url, sizeof last_url);
    retried = false;
    st.failed = false;
    st.ended = false;
    if (remote(last_url) && ytdlp_ensure() != 2) {
        waiting = true;
        st.loading = true;
        return;
    }
    waiting = false;
    go();
}

void player_pause(bool pause) {
    if (!mpv) return;
    int p = pause;
    mpv_set_property_async(mpv, 0, "pause", MPV_FORMAT_FLAG, &p);
}

void player_toggle(void) {
    if (!mpv) return;
    int pause = !st.paused;
    mpv_set_property_async(mpv, 0, "pause", MPV_FORMAT_FLAG, &pause);
}

void player_seek(double secs) {
    if (!mpv) return;
    char buf[64];
    snprintf(buf, sizeof buf, "%.3f", secs < 0 ? 0 : secs);
    const char *cmd[] = { "seek", buf, "absolute", NULL };
    mpv_command_async(mpv, 0, cmd);
}

void player_volume(double volume) {
    volume_wanted = volume;
    if (mpv) mpv_set_property_async(mpv, 0, "volume", MPV_FORMAT_DOUBLE, &volume);
}

double player_time(void) {
    double t = 0;
    if (mpv && !st.idle) mpv_get_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &t);
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
