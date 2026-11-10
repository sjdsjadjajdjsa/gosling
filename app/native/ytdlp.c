#include <curl/curl.h>
#include <pthread.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include "native.h"

extern char **environ;

enum { MISSING, BUSY, READY, FAILED };

static atomic_int state = MISSING;
static char own[1100];
static char chosen[1100];

#ifdef __APPLE__
static const char asset[] = "yt-dlp_macos";
#elif defined(__aarch64__)
static const char asset[] = "yt-dlp_linux_aarch64";
#else
static const char asset[] = "yt-dlp_linux";
#endif

static bool runnable(const char *path) { return access(path, X_OK) == 0; }

static bool search(char *out, size_t cap) {
    char exe[1024] = { 0 };
#ifdef __APPLE__
    uint32_t n = sizeof exe;
    if (_NSGetExecutablePath(exe, &n) != 0) exe[0] = 0;
#else
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    exe[n > 0 ? n : 0] = 0;
#endif
    char *slash = strrchr(exe, '/');
    if (slash) {
        *slash = 0;
        snprintf(out, cap, "%s/yt-dlp", exe);
        if (runnable(out)) return true;
    }
    if (runnable(own)) {
        snprintf(out, cap, "%s", own);
        return true;
    }
    const char *only = getenv("GOSLING_YTDLP_OWN");
    if (only && *only) return false;
    const char *path = getenv("PATH"), *home = getenv("HOME");
    char dirs[4096];
    snprintf(dirs, sizeof dirs, "%s:/opt/homebrew/bin:/usr/local/bin:%s/.local/bin", path ? path : "", home ? home : "");
    for (char *save, *d = strtok_r(dirs, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
        snprintf(out, cap, "%s/yt-dlp", d);
        if (runnable(out)) return true;
    }
    return false;
}

static size_t write_file(char *p, size_t size, size_t n, void *f) { return fwrite(p, size, n, f); }

static void *fetch(void *arg) {
    (void)arg;
    char part[1200], url[200];
    snprintf(part, sizeof part, "%s.part", own);
    snprintf(url, sizeof url, "https://github.com/yt-dlp/yt-dlp/releases/latest/download/%s", asset);
    FILE *f = fopen(part, "wb");
    bool ok = false;
    if (f) {
        CURL *curl = curl_easy_init();
        long status = 0;
        curl_off_t size = 0;
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &size);
        curl_easy_cleanup(curl);
        ok = fclose(f) == 0 && res == CURLE_OK && status == 200 && size > 1000000;
    }
    if (ok) ok = chmod(part, 0755) == 0 && rename(part, own) == 0;
    if (!ok) unlink(part);
    if (ok) snprintf(chosen, sizeof chosen, "%s", own);
    state = ok ? READY : FAILED;
    ui_wake();
    return NULL;
}

static void *update(void *arg) {
    (void)arg;
    char *argv[] = { own, "-U", NULL };
    posix_spawn_file_actions_t io;
    posix_spawn_file_actions_init(&io);
    posix_spawn_file_actions_addopen(&io, 1, "/dev/null", 1, 0);
    posix_spawn_file_actions_addopen(&io, 2, "/dev/null", 1, 0);
    pid_t pid;
    if (posix_spawn(&pid, own, &io, NULL, argv, environ) == 0) {
        int status;
        waitpid(pid, &status, 0);
        utimes(own, NULL);
    }
    posix_spawn_file_actions_destroy(&io);
    return NULL;
}

static void detach(void *(*fn)(void *)) {
    pthread_t t;
    if (!pthread_create(&t, NULL, fn, NULL)) pthread_detach(t);
}

void ytdlp_init(void) {
    char data[1024], cache[1024];
    platform_paths(data, cache, sizeof data);
    mkdir(data, 0755);
    snprintf(own, sizeof own, "%s/yt-dlp", data);
    if (search(chosen, sizeof chosen)) {
        state = READY;
        struct stat st;
        if (!strcmp(chosen, own) && stat(own, &st) == 0 && time(NULL) - st.st_mtime > 7 * 24 * 3600) detach(update);
    } else {
        snprintf(chosen, sizeof chosen, "%s", own);
    }
}

const char *ytdlp_path(void) { return chosen; }

int ytdlp_ensure(void) {
    int s = state;
    if (s == MISSING || s == FAILED) {
        state = BUSY;
        detach(fetch);
        return BUSY;
    }
    return s;
}

int ytdlp_state(void) { return state; }
