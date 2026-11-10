#include <mpv/client.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "native.h"

static mpv_handle *mpv;
static char last_url[2048];
static bool waiting;
static int attempt, winner = -1;
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

static bool profile_has(const char *profiles, const char *file) {
    DIR *d = opendir(profiles);
    if (!d) return false;
    bool found = false;
    char p[1400];
    for (struct dirent *e; !found && (e = readdir(d));) {
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s/%s", profiles, e->d_name, file);
        found = exists(p);
    }
    closedir(d);
    return found;
}

static bool has_cookies(const char *name, const char *dir) {
    if (!strcmp(name, "safari")) return true;
    if (!strcmp(name, "firefox")) return profile_has(dir, "cookies.sqlite");
    return profile_has(dir, "Cookies") || profile_has(dir, "Network/Cookies");
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
        if (!has_cookies(places[i][0], p)) continue;
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

static const char *clients[] = { NULL, "mweb", "web_music", "tv" };
enum { CLIENTS = sizeof clients / sizeof *clients, ATTEMPTS = CLIENTS * 2 };

static bool youtube(const char *url) { return strstr(url, "youtube.com/") || strstr(url, "youtu.be/"); }

static void winner_file(char *out, size_t cap) {
    char data[1024], cache[1024];
    platform_paths(data, cache, sizeof data);
    snprintf(out, cap, "%s/youtube-access", data);
}

static void remember(int n) {
    if (n == winner) return;
    winner = n;
    char path[1100];
    winner_file(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%d\n", n);
    fclose(f);
}

static int recall(void) {
    if (winner >= 0) return winner;
    char path[1100];
    winner_file(path, sizeof path);
    FILE *f = fopen(path, "r");
    int n = 0;
    if (f) {
        if (fscanf(f, "%d", &n) != 1 || n < 0 || n >= ATTEMPTS) n = 0;
        fclose(f);
    }
    return winner = n;
}

static bool access_options(int n) {
    const char *b = n >= CLIENTS ? browser() : NULL;
    if (n >= CLIENTS && !b) return false;
    char raw[512] = "";
    size_t len = 0;
    const char *js = ytdlp_js();
    if (js) len += snprintf(raw + len, sizeof raw - len, "js-runtimes=%s,", js);
    const char *client = clients[n % CLIENTS];
    if (client) len += snprintf(raw + len, sizeof raw - len, "extractor-args=youtube:player_client=%s,", client);
    if (b) len += snprintf(raw + len, sizeof raw - len, "cookies-from-browser=%s,", b);
    if (len) raw[len - 1] = 0;
    mpv_set_property_string(mpv, "ytdl-raw-options", raw);
    return true;
}

static bool retry_youtube(void) {
    if (!youtube(last_url)) return false;
    int first = recall();
    for (;;) {
        attempt = (attempt + 1) % ATTEMPTS;
        if (attempt == first) return false;
        if (access_options(attempt)) break;
    }
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
    if (youtube(last_url)) {
        attempt = recall();
        if (!access_options(attempt)) access_options(attempt = 0);
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
        case MPV_EVENT_PLAYBACK_RESTART:
              if (st.loading && youtube(last_url)) remember(attempt);
              st.loading = false;
              break;
        case MPV_EVENT_END_FILE: {
            mpv_event_end_file *end = e->data;
            if (end->reason == MPV_END_FILE_REASON_ERROR && retry_youtube()) break;
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
