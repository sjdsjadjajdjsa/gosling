#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include "native.h"

static atomic_int command;
static double command_value;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct { track t; bool has, playing; double duration, position; } now;
static atomic_bool dirty;
static sd_bus *bus;
static int kick[2] = { -1, -1 };

void platform_paths(char *data, char *cache, size_t cap) {
    const char *forced = getenv("GOSLING_HOME");
    if (forced && *forced) {
        snprintf(data, cap, "%s", forced);
        snprintf(cache, cap, "%s/cache", forced);
        return;
    }
    const char *home = getenv("HOME"), *xd = getenv("XDG_DATA_HOME"), *xc = getenv("XDG_CACHE_HOME");
    if (xd && *xd) snprintf(data, cap, "%s/gosling", xd);
    else snprintf(data, cap, "%s/.local/share/gosling", home ? home : ".");
    if (xc && *xc) snprintf(cache, cap, "%s/gosling", xc);
    else snprintf(cache, cap, "%s/.cache/gosling", home ? home : ".");
    char parent[1100];
    snprintf(parent, sizeof parent, "%s/.local", home ? home : ".");
    mkdir(parent, 0755);
    snprintf(parent, sizeof parent, "%s/.local/share", home ? home : ".");
    mkdir(parent, 0755);
    snprintf(parent, sizeof parent, "%s/.cache", home ? home : ".");
    mkdir(parent, 0755);
}

bool platform_pick_folder(char *out, size_t cap) {
    static const char *tools[] = {
        "zenity --file-selection --directory --title='Choose a folder with music in it' 2>/dev/null",
        "kdialog --getexistingdirectory ~ 2>/dev/null",
    };
    for (size_t i = 0; i < sizeof tools / sizeof *tools; i++) {
        FILE *p = popen(tools[i], "r");
        if (!p) continue;
        char line[1024] = { 0 };
        bool got = fgets(line, sizeof line, p) != NULL;
        int status = pclose(p);
        if (status == 127 << 8) continue;
        if (!got) return false;
        line[strcspn(line, "\n")] = 0;
        if (!line[0]) return false;
        snprintf(out, cap, "%s", line);
        return true;
    }
    return false;
}

static void send(int code, double value) {
    command_value = value;
    command = code;
    ui_wake();
}

static int m_toggle(sd_bus_message *m, void *u, sd_bus_error *e) { (void)u; (void)e; send(1, 0); return sd_bus_reply_method_return(m, ""); }
static int m_play(sd_bus_message *m, void *u, sd_bus_error *e) { (void)u; (void)e; send(2, 0); return sd_bus_reply_method_return(m, ""); }
static int m_pause(sd_bus_message *m, void *u, sd_bus_error *e) { (void)u; (void)e; send(3, 0); return sd_bus_reply_method_return(m, ""); }
static int m_next(sd_bus_message *m, void *u, sd_bus_error *e) { (void)u; (void)e; send(4, 0); return sd_bus_reply_method_return(m, ""); }
static int m_prev(sd_bus_message *m, void *u, sd_bus_error *e) { (void)u; (void)e; send(5, 0); return sd_bus_reply_method_return(m, ""); }
static int m_noop(sd_bus_message *m, void *u, sd_bus_error *e) { (void)u; (void)e; return sd_bus_reply_method_return(m, ""); }

static int m_seek(sd_bus_message *m, void *u, sd_bus_error *e) {
    (void)u; (void)e;
    int64_t off = 0;
    sd_bus_message_read(m, "x", &off);
    pthread_mutex_lock(&lock);
    double at = now.position + off / 1e6;
    pthread_mutex_unlock(&lock);
    send(6, at < 0 ? 0 : at);
    return sd_bus_reply_method_return(m, "");
}

static int m_set_position(sd_bus_message *m, void *u, sd_bus_error *e) {
    (void)u; (void)e;
    const char *id;
    int64_t at = 0;
    sd_bus_message_read(m, "ox", &id, &at);
    send(6, at / 1e6);
    return sd_bus_reply_method_return(m, "");
}

static int p_true(sd_bus *b, const char *p, const char *i, const char *prop, sd_bus_message *r, void *u, sd_bus_error *e) {
    (void)b; (void)p; (void)i; (void)prop; (void)u; (void)e;
    return sd_bus_message_append(r, "b", 1);
}

static int p_false(sd_bus *b, const char *p, const char *i, const char *prop, sd_bus_message *r, void *u, sd_bus_error *e) {
    (void)b; (void)p; (void)i; (void)prop; (void)u; (void)e;
    return sd_bus_message_append(r, "b", 0);
}

static int p_identity(sd_bus *b, const char *p, const char *i, const char *prop, sd_bus_message *r, void *u, sd_bus_error *e) {
    (void)b; (void)p; (void)i; (void)prop; (void)u; (void)e;
    return sd_bus_message_append(r, "s", "Gosling");
}

static int p_empty(sd_bus *b, const char *p, const char *i, const char *prop, sd_bus_message *r, void *u, sd_bus_error *e) {
    (void)b; (void)p; (void)i; (void)prop; (void)u; (void)e;
    return sd_bus_message_append(r, "as", 0);
}

static int p_status(sd_bus *b, const char *p, const char *i, const char *prop, sd_bus_message *r, void *u, sd_bus_error *e) {
    (void)b; (void)p; (void)i; (void)prop; (void)u; (void)e;
    pthread_mutex_lock(&lock);
    const char *s = !now.has ? "Stopped" : now.playing ? "Playing" : "Paused";
    pthread_mutex_unlock(&lock);
    return sd_bus_message_append(r, "s", s);
}

static int p_position(sd_bus *b, const char *p, const char *i, const char *prop, sd_bus_message *r, void *u, sd_bus_error *e) {
    (void)b; (void)p; (void)i; (void)prop; (void)u; (void)e;
    pthread_mutex_lock(&lock);
    int64_t at = (int64_t)(now.position * 1e6);
    pthread_mutex_unlock(&lock);
    return sd_bus_message_append(r, "x", at);
}

static int p_volume(sd_bus *b, const char *p, const char *i, const char *prop, sd_bus_message *r, void *u, sd_bus_error *e) {
    (void)b; (void)p; (void)i; (void)prop; (void)u; (void)e;
    return sd_bus_message_append(r, "d", 1.0);
}

static int p_rate(sd_bus *b, const char *p, const char *i, const char *prop, sd_bus_message *r, void *u, sd_bus_error *e) {
    (void)b; (void)p; (void)i; (void)prop; (void)u; (void)e;
    return sd_bus_message_append(r, "d", 1.0);
}

static int entry(sd_bus_message *r, const char *key, const char *type, const void *value) {
    sd_bus_message_open_container(r, 'e', "sv");
    sd_bus_message_append(r, "s", key);
    sd_bus_message_open_container(r, 'v', type);
    if (!strcmp(type, "as")) sd_bus_message_append(r, "as", 1, (const char *)value);
    else if (!strcmp(type, "x")) sd_bus_message_append(r, "x", *(const int64_t *)value);
    else sd_bus_message_append(r, type, value);
    sd_bus_message_close_container(r);
    return sd_bus_message_close_container(r);
}

static int p_metadata(sd_bus *b, const char *p, const char *i, const char *prop, sd_bus_message *r, void *u, sd_bus_error *e) {
    (void)b; (void)p; (void)i; (void)prop; (void)u; (void)e;
    pthread_mutex_lock(&lock);
    track t = now.t;
    bool has = now.has;
    int64_t length = (int64_t)(now.duration * 1e6);
    pthread_mutex_unlock(&lock);
    sd_bus_message_open_container(r, 'a', "{sv}");
    if (has) {
        char id[64], art[1200];
        uint64_t h = 1469598103934665603ull;
        for (const char *s = t.key; *s; s++) h = (h ^ (unsigned char)*s) * 1099511628211ull;
        snprintf(id, sizeof id, "/com/goslingplayer/track/t%016llx", (unsigned long long)h);
        entry(r, "mpris:trackid", "o", id);
        entry(r, "xesam:title", "s", t.title[0] ? t.title : "Gosling");
        if (t.artist[0]) entry(r, "xesam:artist", "as", t.artist);
        if (t.album[0]) entry(r, "xesam:album", "s", t.album);
        if (length > 0) entry(r, "mpris:length", "x", &length);
        if (art_file(t.thumb, art + 7, sizeof art - 7)) {
            memcpy(art, "file://", 7);
            entry(r, "mpris:artUrl", "s", art);
        }
    }
    return sd_bus_message_close_container(r);
}

static const sd_bus_vtable root_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Raise", "", "", m_noop, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Quit", "", "", m_noop, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("CanQuit", "b", p_false, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanRaise", "b", p_false, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("HasTrackList", "b", p_false, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Identity", "s", p_identity, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SupportedUriSchemes", "as", p_empty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SupportedMimeTypes", "as", p_empty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};

static const sd_bus_vtable player_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("PlayPause", "", "", m_toggle, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Play", "", "", m_play, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Pause", "", "", m_pause, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Stop", "", "", m_pause, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Next", "", "", m_next, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Previous", "", "", m_prev, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Seek", "x", "", m_seek, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetPosition", "ox", "", m_set_position, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("OpenUri", "s", "", m_noop, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("PlaybackStatus", "s", p_status, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Metadata", "a{sv}", p_metadata, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Position", "x", p_position, 0, 0),
    SD_BUS_PROPERTY("Volume", "d", p_volume, 0, 0),
    SD_BUS_PROPERTY("Rate", "d", p_rate, 0, 0),
    SD_BUS_PROPERTY("MinimumRate", "d", p_rate, 0, 0),
    SD_BUS_PROPERTY("MaximumRate", "d", p_rate, 0, 0),
    SD_BUS_PROPERTY("CanGoNext", "b", p_true, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanGoPrevious", "b", p_true, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanPlay", "b", p_true, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanPause", "b", p_true, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanSeek", "b", p_true, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("CanControl", "b", p_true, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};

static void *serve(void *arg) {
    (void)arg;
    for (;;) {
        while (sd_bus_process(bus, NULL) > 0) {}
        if (atomic_exchange(&dirty, false)) {
            sd_bus_emit_properties_changed(bus, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", "PlaybackStatus", "Metadata", NULL);
            sd_bus_flush(bus);
        }
        uint64_t until = UINT64_MAX;
        sd_bus_get_timeout(bus, &until);
        int ms = -1;
        if (until != UINT64_MAX) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            uint64_t now_us = (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
            ms = until > now_us ? (int)((until - now_us + 999) / 1000) : 0;
        }
        struct pollfd p[2] = { { sd_bus_get_fd(bus), (short)sd_bus_get_events(bus), 0 }, { kick[0], POLLIN, 0 } };
        if (poll(p, 2, ms) < 0) continue;
        if (p[1].revents & POLLIN) {
            char drain[64];
            while (read(kick[0], drain, sizeof drain) > 0) {}
        }
    }
    return NULL;
}

void np_init(void) {
    if (sd_bus_open_user(&bus) < 0) return;
    sd_bus_add_object_vtable(bus, NULL, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2", root_vtable, NULL);
    sd_bus_add_object_vtable(bus, NULL, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", player_vtable, NULL);
    if (sd_bus_request_name(bus, "org.mpris.MediaPlayer2.gosling", 0) < 0) {
        sd_bus_unref(bus);
        bus = NULL;
        return;
    }
    if (pipe(kick) < 0) return;
    fcntl(kick[0], F_SETFL, O_NONBLOCK);
    fcntl(kick[1], F_SETFL, O_NONBLOCK);
    pthread_t t;
    if (!pthread_create(&t, NULL, serve, NULL)) pthread_detach(t);
}

void np_update(const track *t, double duration, double position, bool playing) {
    if (!bus) return;
    pthread_mutex_lock(&lock);
    bool changed = (t != NULL) != now.has || playing != now.playing || duration != now.duration || (t && (strcmp(t->key, now.t.key) || strcmp(t->title, now.t.title)));
    now.has = t != NULL;
    if (t) now.t = *t;
    now.playing = playing;
    now.duration = duration;
    now.position = position;
    pthread_mutex_unlock(&lock);
    if (changed) {
        dirty = true;
        if (write(kick[1], "", 1) < 0) {}
    }
}

int32_t np_command(double *value) {
    int c = atomic_exchange(&command, 0);
    if (c == 6) *value = command_value;
    return c;
}
