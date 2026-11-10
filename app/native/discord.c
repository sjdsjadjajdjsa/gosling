#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <yyjson.h>
#include "native.h"

#ifndef GOSLING_DISCORD_APP
#define GOSLING_DISCORD_APP ""
#endif

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static struct { track t; bool has, playing; double duration, position, at; unsigned version; } want;
static atomic_bool enabled = true;
static bool started;
static char app[32];

static double wall(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int connect_ipc(void) {
    const char *bases[] = { getenv("XDG_RUNTIME_DIR"), getenv("TMPDIR"), getenv("TMP"), getenv("TEMP"), "/tmp" };
    const char *subs[] = { "", "app/com.discordapp.Discord/", "snap.discord/", ".flatpak/com.discordapp.Discord/xdg-run/" };
    for (size_t b = 0; b < sizeof bases / sizeof *bases; b++) {
        if (!bases[b] || !*bases[b]) continue;
        for (size_t s = 0; s < sizeof subs / sizeof *subs; s++) {
            for (int i = 0; i < 10; i++) {
                struct sockaddr_un addr = { .sun_family = AF_UNIX };
                const char *base = bases[b];
                size_t n = strlen(base);
                snprintf(addr.sun_path, sizeof addr.sun_path, "%s%s%sdiscord-ipc-%d", base, n && base[n - 1] == '/' ? "" : "/", subs[s], i);
                int fd = socket(AF_UNIX, SOCK_STREAM, 0);
                if (fd < 0) return -1;
#ifdef SO_NOSIGPIPE
                int one = 1;
                setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
                if (connect(fd, (struct sockaddr *)&addr, sizeof addr) == 0) return fd;
                close(fd);
            }
        }
    }
    return -1;
}

static bool send_frame(int fd, uint32_t op, const char *json, size_t len) {
    uint32_t head[2] = { op, (uint32_t)len };
    char *buf = malloc(8 + len);
    if (!buf) return false;
    memcpy(buf, head, 8);
    memcpy(buf + 8, json, len);
    size_t done = 0;
    while (done < 8 + len) {
#ifdef MSG_NOSIGNAL
        ssize_t w = send(fd, buf + done, 8 + len - done, MSG_NOSIGNAL);
#else
        ssize_t w = send(fd, buf + done, 8 + len - done, 0);
#endif
        if (w <= 0) {
            free(buf);
            return false;
        }
        done += (size_t)w;
    }
    free(buf);
    return true;
}

static bool read_frame(int fd, int timeout_ms, uint32_t *op) {
    struct pollfd p = { fd, POLLIN, 0 };
    if (poll(&p, 1, timeout_ms) <= 0) return timeout_ms == 0;
    uint32_t head[2];
    if (recv(fd, head, 8, MSG_WAITALL) != 8) return false;
    *op = head[0];
    char drop[512];
    for (uint32_t left = head[1]; left > 0;) {
        ssize_t r = recv(fd, drop, left < sizeof drop ? left : sizeof drop, 0);
        if (r <= 0) return false;
        left -= (uint32_t)r;
    }
    return true;
}

static bool drain(int fd) {
    for (;;) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 0) <= 0) return true;
        if (p.revents & (POLLHUP | POLLERR)) return false;
        uint32_t op;
        if (!read_frame(fd, 0, &op) || op == 2) return false;
    }
}

static bool handshake(int fd) {
    char json[128];
    int n = snprintf(json, sizeof json, "{\"v\":1,\"client_id\":\"%s\"}", app);
    uint32_t op = 0;
    return send_frame(fd, 0, json, (size_t)n) && read_frame(fd, 5000, &op) && op == 1;
}

static void cut(char *out, size_t cap, const char *s) {
    snprintf(out, cap, "%s", s);
    size_t n = strlen(out);
    while (n > 0 && ((unsigned char)out[n - 1] & 0xC0) == 0x80) n--;
    if (n > 0 && (unsigned char)out[n - 1] >= 0xC0 && strlen(out) != n) out[n - 1] = 0;
}

static bool set_activity(int fd, const track *t, bool playing, double duration, double position, double at, unsigned nonce) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "cmd", "SET_ACTIVITY");
    char nonce_s[32];
    snprintf(nonce_s, sizeof nonce_s, "%u", nonce);
    yyjson_mut_obj_add_strcpy(doc, root, "nonce", nonce_s);
    yyjson_mut_val *args = yyjson_mut_obj_add_obj(doc, root, "args");
    yyjson_mut_obj_add_int(doc, args, "pid", getpid());
    if (!t || !playing) {
        yyjson_mut_obj_add_null(doc, args, "activity");
    } else {
        char title[128], artist[128], album[128];
        cut(title, sizeof title, t->title[0] ? t->title : "Gosling");
        if (strlen(title) < 2) strcat(title, "  ");
        cut(artist, sizeof artist, t->artist);
        cut(album, sizeof album, t->album);
        yyjson_mut_val *a = yyjson_mut_obj_add_obj(doc, args, "activity");
        yyjson_mut_obj_add_int(doc, a, "type", 2);
        yyjson_mut_obj_add_int(doc, a, "status_display_type", 1);
        yyjson_mut_obj_add_strcpy(doc, a, "details", title);
        if (strlen(artist) >= 2) yyjson_mut_obj_add_strcpy(doc, a, "state", artist);
        if (duration > 0) {
            yyjson_mut_val *ts = yyjson_mut_obj_add_obj(doc, a, "timestamps");
            double start = at - position;
            yyjson_mut_obj_add_int(doc, ts, "start", (int64_t)(start * 1000));
            yyjson_mut_obj_add_int(doc, ts, "end", (int64_t)((start + duration) * 1000));
        }
        yyjson_mut_val *assets = yyjson_mut_obj_add_obj(doc, a, "assets");
        bool web = !strncmp(t->thumb, "https://", 8);
        yyjson_mut_obj_add_strcpy(doc, assets, "large_image", web ? t->thumb : "gosling");
        if (strlen(album) >= 2) yyjson_mut_obj_add_strcpy(doc, assets, "large_text", album);
        if (!track_local(t) && !strstr(t->key, "://")) {
            char url[160];
            snprintf(url, sizeof url, "https://music.youtube.com/watch?v=%s", t->key);
            yyjson_mut_val *buttons = yyjson_mut_obj_add_arr(doc, a, "buttons");
            yyjson_mut_val *b = yyjson_mut_arr_add_obj(doc, buttons);
            yyjson_mut_obj_add_str(doc, b, "label", "Listen on YouTube Music");
            yyjson_mut_obj_add_strcpy(doc, b, "url", url);
        }
    }
    size_t len = 0;
    char *json = yyjson_mut_write(doc, 0, &len);
    yyjson_mut_doc_free(doc);
    if (!json) return false;
    bool ok = send_frame(fd, 1, json, len);
    free(json);
    return ok;
}

static void *run(void *arg) {
    (void)arg;
    int fd = -1;
    unsigned sent = 0, nonce = 0;
    for (;;) {
        pthread_mutex_lock(&lock);
        if (fd >= 0 && sent == want.version) {
            struct timespec until;
            clock_gettime(CLOCK_REALTIME, &until);
            until.tv_sec += 5;
            pthread_cond_timedwait(&changed, &lock, &until);
        }
        unsigned version = want.version;
        track t = want.t;
        bool has = want.has && enabled, playing = want.playing;
        double duration = want.duration, position = want.position, at = want.at;
        pthread_mutex_unlock(&lock);

        if (fd >= 0 && !drain(fd)) {
            close(fd);
            fd = -1;
        }
        if (fd < 0) {
            if (!has && sent == version) {
                sleep(15);
                continue;
            }
            fd = connect_ipc();
            if (fd >= 0 && !handshake(fd)) {
                close(fd);
                fd = -1;
            }
            if (fd < 0) {
                sleep(15);
                continue;
            }
            sent = version - 1;
        }
        if (sent != version) {
            if (!set_activity(fd, has ? &t : NULL, playing, duration, position, at, ++nonce)) {
                close(fd);
                fd = -1;
                continue;
            }
            sent = version;
        }
    }
    return NULL;
}

void discord_init(bool on) {
    const char *forced = getenv("GOSLING_DISCORD_APP");
    snprintf(app, sizeof app, "%s", forced && *forced ? forced : GOSLING_DISCORD_APP);
    enabled = on;
    for (const char *c = app; *c; c++)
        if (*c < '0' || *c > '9') app[0] = 0;
    if (!app[0] || started) return;
    pthread_t th;
    started = !pthread_create(&th, NULL, run, NULL);
    if (started) pthread_detach(th);
}

bool discord_available(void) { return app[0] != 0; }

void discord_enable(bool on) {
    pthread_mutex_lock(&lock);
    enabled = on;
    want.version++;
    pthread_cond_signal(&changed);
    pthread_mutex_unlock(&lock);
}

void discord_update(const track *t, double duration, double position, bool playing) {
    if (!started) return;
    pthread_mutex_lock(&lock);
    want.has = t != NULL;
    if (t) want.t = *t;
    want.playing = playing;
    want.duration = duration;
    want.position = position;
    want.at = wall();
    want.version++;
    pthread_cond_signal(&changed);
    pthread_mutex_unlock(&lock);
}
