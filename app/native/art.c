#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "native.h"

enum { SLOTS = 48, PX = 56 };
enum { EMPTY, WANTED, LOADING, READY, FAILED };

typedef struct {
    uint64_t hash;
    int state;
    unsigned used;
    char url[512];
    void *image;
} slot;

static slot slots[SLOTS];
static unsigned tick;
static char dir[1024];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static bool started;

static uint64_t fnv(const char *s) {
    uint64_t h = 1469598103934665603ull;
    while (*s) h = (h ^ (unsigned char)*s++) * 1099511628211ull;
    return h ? h : 1;
}

static void sized(const char *url, char *out, size_t cap) {
    const char *eq = strrchr(url, '=');
    if (strstr(url, "googleusercontent.com") && eq) snprintf(out, cap, "%.*s=w%d-h%d-l90-rj", (int)(eq - url), url, PX * 2, PX * 2);
    else snprintf(out, cap, "%s", url);
}

static void cache_path(const char *url, char *out, size_t cap) {
    if (!strncmp(url, "file://", 7)) snprintf(out, cap, "%s", url + 7);
    else snprintf(out, cap, "%s/%016llx", dir, (unsigned long long)fnv(url));
}

typedef struct { unsigned char *data; size_t len; } buffer;

static size_t collect(char *p, size_t size, size_t n, void *user) {
    buffer *b = user;
    unsigned char *grown = realloc(b->data, b->len + size * n);
    if (!grown) return 0;
    b->data = grown;
    memcpy(b->data + b->len, p, size * n);
    b->len += size * n;
    return size * n;
}

static bool read_file(const char *path, buffer *b) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b->data = n > 0 ? malloc((size_t)n) : NULL;
    b->len = b->data ? fread(b->data, 1, (size_t)n, f) : 0;
    fclose(f);
    return b->len > 0;
}

static bool fetch(const char *url, buffer *b) {
    char real[600];
    sized(url, real, sizeof real);
    CURL *curl = curl_easy_init();
    long status = 0;
    curl_easy_setopt(curl, CURLOPT_URL, real);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, b);
    CURLcode ok = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    return ok == CURLE_OK && status == 200 && b->len > 0;
}

static void *work(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&lock);
        slot *job = NULL;
        while (!job) {
            for (int i = 0; i < SLOTS && !job; i++)
                if (slots[i].state == WANTED) job = &slots[i];
            if (!job) pthread_cond_wait(&wake, &lock);
        }
        job->state = LOADING;
        char url[512], path[1100];
        uint64_t hash = job->hash;
        snprintf(url, sizeof url, "%s", job->url);
        pthread_mutex_unlock(&lock);

        cache_path(url, path, sizeof path);
        buffer b = { 0 };
        bool ok = read_file(path, &b);
        if (!ok && strncmp(url, "file://", 7)) {
            free(b.data);
            b = (buffer){ 0 };
            ok = fetch(url, &b);
            if (ok) {
                FILE *f = fopen(path, "wb");
                if (f) {
                    fwrite(b.data, 1, b.len, f);
                    fclose(f);
                }
            }
        }
        void *img = ok ? ui_image_decode(b.data, b.len, PX * 2) : NULL;
        free(b.data);

        pthread_mutex_lock(&lock);
        if (job->hash == hash && job->state == LOADING) {
            job->image = img;
            job->state = img ? READY : FAILED;
        } else if (img) {
            ui_image_free(img);
        }
        pthread_mutex_unlock(&lock);
        ui_wake();
    }
    return NULL;
}

void art_init(const char *cache) {
    snprintf(dir, sizeof dir, "%s/art", cache);
    mkdir(dir, 0755);
}

static slot *find(const char *url) {
    uint64_t h = fnv(url);
    slot *victim = NULL;
    for (int i = 0; i < SLOTS; i++) {
        if (slots[i].hash == h) return &slots[i];
        if (slots[i].state == LOADING) continue;
        if (!victim || slots[i].used < victim->used) victim = &slots[i];
    }
    if (!victim) return NULL;
    if (victim->state == READY) ui_image_free(victim->image);
    victim->image = NULL;
    victim->hash = h;
    victim->state = WANTED;
    snprintf(victim->url, sizeof victim->url, "%s", url);
    if (!started) {
        pthread_t t;
        started = !pthread_create(&t, NULL, work, NULL);
        if (started) pthread_detach(t);
    }
    pthread_cond_signal(&wake);
    return victim;
}

static void note(float x, float y, float s, uint32_t c) {
    float u = s / 40.0f;
    ui_circle(x + 16 * u, y + 26 * u, 4.5f * u, c);
    ui_rect(x + 19 * u, y + 11 * u, 2.2f * u, 15.5f * u, 0, c);
    ui_triangle(x + 19 * u, y + 11 * u, x + 21.2f * u, y + 18 * u, x + 27 * u, y + 15 * u, c);
}

void art_draw(const char *url, float x, float y, float size, uint32_t bg, uint32_t fg) {
    void *image = NULL;
    if (url && url[0]) {
        pthread_mutex_lock(&lock);
        slot *s = find(url);
        if (s) {
            s->used = ++tick;
            if (s->state == READY) image = s->image;
        }
        pthread_mutex_unlock(&lock);
    }
    if (image) {
        ui_image_draw(image, x, y, size, size * 0.18f);
        return;
    }
    ui_rect(x, y, size, size, size * 0.18f, bg);
    note(x + size * 0.1f, y + size * 0.1f, size * 0.8f, fg);
}

bool art_file(const char *url, char *out, size_t cap) {
    if (!url || !url[0]) return false;
    cache_path(url, out, cap);
    struct stat st;
    return stat(out, &st) == 0;
}
