#include <curl/curl.h>
#include <pthread.h>
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "native.h"

enum { SLOTS = 96, PX = 112 };
enum { EMPTY, WANTED, LOADING, DECODED, READY, FAILED };

typedef struct {
    uint64_t hash;
    int state;
    unsigned used;
    char url[512];
    Image image;
    Texture2D texture;
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

static Image decode(const buffer *b) {
    const char *type = b->len > 4 && !memcmp(b->data, "\x89PNG", 4) ? ".png" : ".jpg";
    Image img = LoadImageFromMemory(type, b->data, (int)b->len);
    if (!img.data) return img;
    int side = img.width < img.height ? img.width : img.height;
    if (img.width != img.height) ImageCrop(&img, (Rectangle){ (float)(img.width - side) / 2, (float)(img.height - side) / 2, (float)side, (float)side });
    if (side > PX * 2) ImageResize(&img, PX * 2, PX * 2);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    return img;
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
        Image img = ok ? decode(&b) : (Image){ 0 };
        free(b.data);

        pthread_mutex_lock(&lock);
        if (job->hash == hash && job->state == LOADING) {
            job->image = img;
            job->state = img.data ? DECODED : FAILED;
        } else if (img.data) {
            UnloadImage(img);
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
    if (victim->state == READY) UnloadTexture(victim->texture);
    if (victim->state == DECODED) UnloadImage(victim->image);
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

static void note(float x, float y, float s, Color c) {
    float u = s / 40.0f;
    DrawCircleV((Vector2){ x + 16 * u, y + 26 * u }, 4.5f * u, c);
    DrawRectangleRec((Rectangle){ x + 19 * u, y + 11 * u, 2.2f * u, 15.5f * u }, c);
    DrawTriangle((Vector2){ x + 19 * u, y + 11 * u }, (Vector2){ x + 21.2f * u, y + 18 * u }, (Vector2){ x + 27 * u, y + 15 * u }, c);
}

static Color rgba(uint32_t c) {
    return (Color){ (unsigned char)(c >> 24), (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c };
}

void art_draw(const char *url, float x, float y, float size, uint32_t bg, uint32_t fg) {
    Rectangle dst = { x, y, size, size };
    slot *s = NULL;
    if (url && url[0]) {
        pthread_mutex_lock(&lock);
        s = find(url);
        if (s) {
            s->used = ++tick;
            if (s->state == DECODED) {
                s->texture = LoadTextureFromImage(s->image);
                GenTextureMipmaps(&s->texture);
                SetTextureFilter(s->texture, TEXTURE_FILTER_TRILINEAR);
                UnloadImage(s->image);
                s->state = READY;
            }
        }
        pthread_mutex_unlock(&lock);
    }
    if (s && s->state == READY) {
        DrawTexturePro(s->texture, (Rectangle){ 0, 0, (float)s->texture.width, (float)s->texture.height }, dst, (Vector2){ 0 }, 0, WHITE);
        return;
    }
    DrawRectangleRounded(dst, 0.12f, 6, rgba(bg));
    note(x + size * 0.1f, y + size * 0.1f, size * 0.8f, rgba(fg));
}

bool art_file(const char *url, char *out, size_t cap) {
    if (!url || !url[0]) return false;
    cache_path(url, out, cap);
    struct stat st;
    return stat(out, &st) == 0;
}
