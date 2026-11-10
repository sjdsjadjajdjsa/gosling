#include <fts.h>
#include <sys/stat.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include "native.h"

typedef struct { char db[1024], folder[1024], cache[1024]; } job;

static atomic_int busy, changes;
static pthread_mutex_t one = PTHREAD_MUTEX_INITIALIZER;
static __thread sqlite3 *db;
static __thread sqlite3_stmt *insert;
static __thread const job *current;
static __thread int pending;

static bool audio(const char *path) {
    static const char *exts[] = { ".mp3", ".flac", ".m4a", ".aac", ".ogg", ".oga", ".opus", ".wav", ".aif", ".aiff", ".alac", ".wma", ".ape", ".wv", ".mka" };
    const char *dot = strrchr(path, '.');
    if (!dot) return false;
    for (size_t i = 0; i < sizeof exts / sizeof *exts; i++)
        if (!strcasecmp(dot, exts[i])) return true;
    return false;
}

static const char *tag(AVFormatContext *f, const char *key) {
    AVDictionaryEntry *e = av_dict_get(f->metadata, key, NULL, 0);
    for (unsigned i = 0; !e && i < f->nb_streams; i++) e = av_dict_get(f->streams[i]->metadata, key, NULL, 0);
    return e ? e->value : NULL;
}

static uint64_t fnv(const char *s) {
    uint64_t h = 1469598103934665603ull;
    while (*s) h = (h ^ (unsigned char)*s++) * 1099511628211ull;
    return h;
}

static void cover(AVFormatContext *f, const char *album, const char *artist, char *thumb, size_t cap) {
    for (unsigned i = 0; i < f->nb_streams; i++) {
        AVStream *s = f->streams[i];
        if (!(s->disposition & AV_DISPOSITION_ATTACHED_PIC) || s->attached_pic.size <= 0) continue;
        char key[600], path[1200];
        snprintf(key, sizeof key, "%s\x1f%s", album, artist);
        snprintf(path, sizeof path, "%s/art/local-%016llx", current->cache, (unsigned long long)fnv(key));
        FILE *out = fopen(path, "wbx");
        if (out) {
            fwrite(s->attached_pic.data, 1, (size_t)s->attached_pic.size, out);
            fclose(out);
        }
        snprintf(thumb, cap, "file://%s", path);
        return;
    }
}

static void add(const char *path) {
    AVFormatContext *f = NULL;
    if (avformat_open_input(&f, path, NULL, NULL) < 0) return;
    if (f->duration <= 0) avformat_find_stream_info(f, NULL);
    track t = { 0 };
    snprintf(t.key, sizeof t.key, "%s", path);
    const char *title = tag(f, "title"), *artist = tag(f, "artist"), *album = tag(f, "album"), *number = tag(f, "track");
    if (!artist) artist = tag(f, "album_artist");
    if (title) snprintf(t.title, sizeof t.title, "%s", title);
    else {
        const char *base = strrchr(path, '/');
        snprintf(t.title, sizeof t.title, "%s", base ? base + 1 : path);
        char *dot = strrchr(t.title, '.');
        if (dot) *dot = 0;
    }
    snprintf(t.artist, sizeof t.artist, "%s", artist ? artist : "");
    snprintf(t.album, sizeof t.album, "%s", album ? album : "");
    t.duration = f->duration > 0 ? (double)f->duration / AV_TIME_BASE : 0;
    cover(f, t.album, t.artist, t.thumb, sizeof t.thumb);
    int track_number = number ? atoi(number) : 0;
    avformat_close_input(&f);

    sqlite3_reset(insert);
    sqlite3_bind_text(insert, 1, t.key, -1, SQLITE_STATIC);
    sqlite3_bind_text(insert, 2, t.title, -1, SQLITE_STATIC);
    sqlite3_bind_text(insert, 3, t.artist, -1, SQLITE_STATIC);
    sqlite3_bind_text(insert, 4, t.album, -1, SQLITE_STATIC);
    sqlite3_bind_text(insert, 5, t.thumb, -1, SQLITE_STATIC);
    sqlite3_bind_double(insert, 6, t.duration);
    sqlite3_bind_text(insert, 7, current->folder, -1, SQLITE_STATIC);
    sqlite3_bind_int(insert, 8, track_number);
    sqlite3_step(insert);
    if (++pending % 200 == 0) {
        sqlite3_exec(db, "COMMIT; BEGIN", NULL, NULL, NULL);
        changes++;
        ui_wake();
    }
}

static void walk(const char *folder) {
    char *roots[] = { (char *)folder, NULL };
    FTS *fts = fts_open(roots, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
    if (!fts) return;
    for (FTSENT *e; (e = fts_read(fts));) {
        if (e->fts_info == FTS_D && e->fts_level > 0 && e->fts_name[0] == '.') fts_set(fts, e, FTS_SKIP);
        else if (e->fts_info == FTS_F && audio(e->fts_path)) add(e->fts_path);
    }
    fts_close(fts);
}

static void prune(const char *folder) {
    sqlite3_stmt *s, *del;
    sqlite3_prepare_v2(db, "SELECT key FROM tracks WHERE folder = ?1", -1, &s, NULL);
    sqlite3_prepare_v2(db, "DELETE FROM tracks WHERE key = ?1", -1, &del, NULL);
    sqlite3_bind_text(s, 1, folder, -1, SQLITE_STATIC);
    struct stat st;
    while (sqlite3_step(s) == SQLITE_ROW) {
        const char *key = (const char *)sqlite3_column_text(s, 0);
        if (stat(key, &st) == 0) continue;
        sqlite3_reset(del);
        sqlite3_bind_text(del, 1, key, -1, SQLITE_TRANSIENT);
        sqlite3_step(del);
    }
    sqlite3_finalize(s);
    sqlite3_finalize(del);
}

static void *run(void *arg) {
    job *j = arg;
    pthread_mutex_lock(&one);
    current = j;
    av_log_set_level(AV_LOG_QUIET);
    if (sqlite3_open(j->db, &db) == SQLITE_OK) {
        sqlite3_busy_timeout(db, 5000);
        sqlite3_prepare_v2(db,
            "INSERT INTO tracks(key, title, artist, album, thumb, duration, folder, number) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) "
            "ON CONFLICT(key) DO UPDATE SET title = ?2, artist = ?3, album = ?4, thumb = ?5, duration = ?6, folder = ?7, number = ?8",
            -1, &insert, NULL);
        sqlite3_exec(db, "BEGIN", NULL, NULL, NULL);
        walk(j->folder);
        prune(j->folder);
        sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);
        sqlite3_finalize(insert);
        sqlite3_close(db);
    }
    pthread_mutex_unlock(&one);
    free(j);
    busy--;
    changes++;
    ui_wake();
    return NULL;
}

void scan_start(const char *db_path, const char *folder, const char *cache) {
    job *j = malloc(sizeof *j);
    if (!j) return;
    snprintf(j->db, sizeof j->db, "%s", db_path);
    snprintf(j->folder, sizeof j->folder, "%s", folder);
    snprintf(j->cache, sizeof j->cache, "%s", cache);
    busy++;
    pthread_t t;
    if (pthread_create(&t, NULL, run, j)) {
        busy--;
        free(j);
        return;
    }
    pthread_detach(t);
}

int32_t scan_busy(void) { return busy; }
int32_t scan_changes(void) { return changes; }
