#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <math.h>
#include <time.h>
#include "native.h"

enum { V_SEARCH, V_LIKED, V_HISTORY, V_LOCAL, V_PLAYLIST, V_QUEUE };

typedef struct { track *items; int len, cap; } list;

static sqlite3 *db;
static char db_path[1024], cache_dir[1024];
static int view_kind = V_SEARCH;
static int64_t view_playlist;
static list view, queue;
static int queue_pos = -1;
static int64_t *playlist_ids;
static char (*playlist_names)[128];
static int playlist_count;
static int seen_scan;

static const char schema[] =
    "PRAGMA journal_mode=WAL;"
    "PRAGMA busy_timeout=3000;"
    "CREATE TABLE IF NOT EXISTS tracks(key TEXT PRIMARY KEY, title TEXT, artist TEXT, album TEXT, thumb TEXT, duration REAL, folder TEXT, number INT);"
    "CREATE TABLE IF NOT EXISTS liked(key TEXT PRIMARY KEY, at INT);"
    "CREATE TABLE IF NOT EXISTS history(id INTEGER PRIMARY KEY, key TEXT, at INT);"
    "CREATE INDEX IF NOT EXISTS history_key ON history(key);"
    "CREATE TABLE IF NOT EXISTS playlists(id INTEGER PRIMARY KEY, name TEXT);"
    "CREATE TABLE IF NOT EXISTS playlist_items(id INTEGER PRIMARY KEY, playlist INT, key TEXT);"
    "CREATE TABLE IF NOT EXISTS folders(path TEXT PRIMARY KEY);"
    "CREATE TABLE IF NOT EXISTS kv(key TEXT PRIMARY KEY, value REAL);";

static void push(list *l, const track *t) {
    if (l->len == l->cap) {
        int cap = l->cap ? l->cap * 2 : 64;
        track *grown = realloc(l->items, (size_t)cap * sizeof *grown);
        if (!grown) return;
        l->items = grown;
        l->cap = cap;
    }
    l->items[l->len++] = *t;
}

static void text(sqlite3_stmt *s, int col, char *out, size_t cap) {
    const unsigned char *v = sqlite3_column_text(s, col);
    snprintf(out, cap, "%s", v ? (const char *)v : "");
}

static sqlite3_stmt *prepare(const char *sql) {
    sqlite3_stmt *s = NULL;
    sqlite3_prepare_v2(db, sql, -1, &s, NULL);
    return s;
}

static void save(const track *t) {
    sqlite3_stmt *s = prepare(
        "INSERT INTO tracks(key, title, artist, album, thumb, duration) VALUES(?1, ?2, ?3, ?4, ?5, ?6) "
        "ON CONFLICT(key) DO UPDATE SET title = CASE WHEN folder IS NULL THEN ?2 ELSE title END, "
        "artist = CASE WHEN folder IS NULL THEN ?3 ELSE artist END, album = CASE WHEN folder IS NULL THEN ?4 ELSE album END, "
        "thumb = CASE WHEN ?5 = '' THEN thumb ELSE ?5 END, duration = CASE WHEN ?6 > 0 THEN ?6 ELSE duration END");
    sqlite3_bind_text(s, 1, t->key, -1, SQLITE_STATIC);
    sqlite3_bind_text(s, 2, t->title, -1, SQLITE_STATIC);
    sqlite3_bind_text(s, 3, t->artist, -1, SQLITE_STATIC);
    sqlite3_bind_text(s, 4, t->album, -1, SQLITE_STATIC);
    sqlite3_bind_text(s, 5, t->thumb, -1, SQLITE_STATIC);
    sqlite3_bind_double(s, 6, t->duration);
    sqlite3_step(s);
    sqlite3_finalize(s);
}

static void load_playlists(void) {
    playlist_count = 0;
    sqlite3_stmt *s = prepare("SELECT id, name FROM playlists ORDER BY id");
    int cap = 0;
    while (sqlite3_step(s) == SQLITE_ROW) {
        if (playlist_count == cap) {
            cap = cap ? cap * 2 : 16;
            int64_t *ids = realloc(playlist_ids, (size_t)cap * sizeof *ids);
            if (ids) playlist_ids = ids;
            char (*names)[128] = realloc(playlist_names, (size_t)cap * sizeof *names);
            if (names) playlist_names = names;
            if (!ids || !names) break;
        }
        playlist_ids[playlist_count] = sqlite3_column_int64(s, 0);
        text(s, 1, playlist_names[playlist_count], sizeof playlist_names[0]);
        playlist_count++;
    }
    sqlite3_finalize(s);
}

static void fill(const char *sql, int64_t arg) {
    view.len = 0;
    sqlite3_stmt *s = prepare(sql);
    if (arg) sqlite3_bind_int64(s, 1, arg);
    while (sqlite3_step(s) == SQLITE_ROW) {
        track t = { 0 };
        text(s, 0, t.key, sizeof t.key);
        text(s, 1, t.title, sizeof t.title);
        text(s, 2, t.artist, sizeof t.artist);
        text(s, 3, t.album, sizeof t.album);
        text(s, 4, t.thumb, sizeof t.thumb);
        t.duration = sqlite3_column_double(s, 5);
        t.item = sqlite3_column_int64(s, 6);
        push(&view, &t);
    }
    sqlite3_finalize(s);
}

#define COLUMNS "t.key, t.title, t.artist, t.album, t.thumb, t.duration"

static void reload(void) {
    switch (view_kind) {
    case V_LIKED: fill("SELECT " COLUMNS ", 0 FROM liked l JOIN tracks t ON t.key = l.key ORDER BY l.rowid DESC", 0); break;
    case V_HISTORY: fill("SELECT " COLUMNS ", 0 FROM (SELECT key, MAX(id) AS last FROM history GROUP BY key ORDER BY last DESC LIMIT 500) h JOIN tracks t ON t.key = h.key ORDER BY h.last DESC", 0); break;
    case V_LOCAL: fill("SELECT " COLUMNS ", 0 FROM tracks t WHERE folder IS NOT NULL ORDER BY artist COLLATE NOCASE, album COLLATE NOCASE, number, title COLLATE NOCASE", 0); break;
    case V_PLAYLIST: fill("SELECT " COLUMNS ", p.id FROM playlist_items p JOIN tracks t ON t.key = p.key WHERE p.playlist = ?1 ORDER BY p.id", view_playlist); break;
    default: view.len = 0; break;
    }
}

static const track *row(int i) {
    static track found;
    if (view_kind == V_SEARCH) return search_track(i, &found) ? &found : NULL;
    if (view_kind == V_QUEUE) {
        int q = queue_pos + 1 + i;
        return q >= 0 && q < queue.len ? &queue.items[q] : NULL;
    }
    return i >= 0 && i < view.len ? &view.items[i] : NULL;
}

void lib_open(void) {
    char data[1024];
    platform_paths(data, cache_dir, sizeof data);
    mkdir(data, 0755);
    mkdir(cache_dir, 0755);
    snprintf(db_path, sizeof db_path, "%s/gosling.db", data);
    if (sqlite3_open(db_path, &db) != SQLITE_OK) return;
    sqlite3_exec(db, schema, NULL, NULL, NULL);
    load_playlists();
    art_init(cache_dir);
}

void lib_close(void) {
    sqlite3_close(db);
    db = NULL;
}

double lib_get(gs_str key, double fallback) {
    char k[128];
    sqlite3_stmt *s = prepare("SELECT value FROM kv WHERE key = ?1");
    sqlite3_bind_text(s, 1, gs_cstr(key, k, sizeof k), -1, SQLITE_STATIC);
    double v = sqlite3_step(s) == SQLITE_ROW ? sqlite3_column_double(s, 0) : fallback;
    sqlite3_finalize(s);
    return v;
}

void lib_set(gs_str key, double value) {
    char k[128];
    sqlite3_stmt *s = prepare("INSERT OR REPLACE INTO kv VALUES(?1, ?2)");
    sqlite3_bind_text(s, 1, gs_cstr(key, k, sizeof k), -1, SQLITE_STATIC);
    sqlite3_bind_double(s, 2, value);
    sqlite3_step(s);
    sqlite3_finalize(s);
}

void lib_show(int32_t kind, int64_t playlist) {
    view_kind = kind;
    view_playlist = playlist;
    reload();
}

int32_t lib_view(void) { return view_kind; }
int64_t lib_view_playlist(void) { return view_playlist; }

void lib_refresh(void) {
    int changes = scan_changes();
    if (changes != seen_scan) {
        seen_scan = changes;
        if (view_kind == V_LOCAL) reload();
    }
}

int32_t lib_count(void) {
    if (view_kind == V_SEARCH) return search_count();
    if (view_kind == V_QUEUE) return queue.len - queue_pos - 1 > 0 ? queue.len - queue_pos - 1 : 0;
    return view.len;
}

int32_t lib_field(int32_t i, int32_t field, char out[256]) {
    const track *t = row(i);
    return t ? gs_copy(out, 256, track_field(t, field)) : 0;
}

double lib_duration(int32_t i) {
    const track *t = row(i);
    return t ? t->duration : 0;
}

void lib_art(int32_t i, float x, float y, float size, uint32_t bg, uint32_t fg) {
    const track *t = row(i);
    art_draw(t ? t->thumb : "", x, y, size, bg, fg);
}

bool lib_current_row(int32_t i) {
    const track *t = row(i);
    return t && queue_pos >= 0 && queue_pos < queue.len && !strcmp(t->key, queue.items[queue_pos].key);
}

static bool is_liked(const char *key) {
    sqlite3_stmt *s = prepare("SELECT 1 FROM liked WHERE key = ?1");
    sqlite3_bind_text(s, 1, key, -1, SQLITE_STATIC);
    bool yes = sqlite3_step(s) == SQLITE_ROW;
    sqlite3_finalize(s);
    return yes;
}

static void set_liked(const track *t, bool like) {
    if (like) save(t);
    sqlite3_stmt *s = prepare(like ? "INSERT OR REPLACE INTO liked VALUES(?1, ?2)" : "DELETE FROM liked WHERE key = ?1");
    sqlite3_bind_text(s, 1, t->key, -1, SQLITE_STATIC);
    if (like) sqlite3_bind_int64(s, 2, (int64_t)time(NULL));
    sqlite3_step(s);
    sqlite3_finalize(s);
}

bool lib_liked(int32_t i) {
    const track *t = row(i);
    return t && is_liked(t->key);
}

void lib_like(int32_t i) {
    const track *t = row(i);
    if (!t) return;
    track copy = *t;
    set_liked(&copy, !is_liked(copy.key));
    if (view_kind == V_LIKED) reload();
}

static track *current(void) { return queue_pos >= 0 && queue_pos < queue.len ? &queue.items[queue_pos] : NULL; }

bool lib_current_liked(void) {
    track *t = current();
    return t && t->title[0] && is_liked(t->key);
}

void lib_current_like(void) {
    track *t = current();
    if (!t || !t->title[0]) return;
    set_liked(t, !is_liked(t->key));
    if (view_kind == V_LIKED) reload();
}

int32_t lib_current_field(int32_t field, char out[256]) {
    track *t = current();
    return t ? gs_copy(out, 256, track_field(t, field)) : 0;
}

int32_t lib_current_url(char out[1024]) {
    track *t = current();
    if (!t) return 0;
    char url[1100];
    if (track_local(t) || strstr(t->key, "://")) snprintf(url, sizeof url, "%s", t->key);
    else snprintf(url, sizeof url, "https://music.youtube.com/watch?v=%s", t->key);
    return gs_copy(out, 1024, url);
}

void lib_current_art(float x, float y, float size, uint32_t bg, uint32_t fg) {
    track *t = current();
    art_draw(t ? t->thumb : "", x, y, size, bg, fg);
}

void lib_current_title(gs_str title) {
    track *t = current();
    if (!t || t->title[0]) return;
    gs_cstr(title, t->title, sizeof t->title);
}

void lib_played(void) {
    track *t = current();
    if (!t || !t->title[0] || strstr(t->key, "://")) return;
    save(t);
    sqlite3_stmt *s = prepare("INSERT INTO history(key, at) VALUES(?1, ?2)");
    sqlite3_bind_text(s, 1, t->key, -1, SQLITE_STATIC);
    sqlite3_bind_int64(s, 2, (int64_t)time(NULL));
    sqlite3_step(s);
    sqlite3_finalize(s);
    if (view_kind == V_HISTORY) reload();
}

void lib_play_row(int32_t i) {
    if (view_kind == V_QUEUE) {
        int q = queue_pos + 1 + i;
        if (q < queue.len) queue_pos = q;
        return;
    }
    int n = lib_count();
    queue.len = 0;
    for (int k = 0; k < n; k++) {
        const track *t = row(k);
        if (t) push(&queue, t);
    }
    queue_pos = i < queue.len ? i : queue.len - 1;
}

void lib_play_url(gs_str url) {
    track t = { 0 };
    gs_cstr(url, t.key, sizeof t.key);
    queue.len = 0;
    push(&queue, &t);
    queue_pos = 0;
}

void lib_enqueue(int32_t i) {
    const track *t = row(i);
    if (!t) return;
    track copy = *t;
    if (queue_pos < 0) {
        queue.len = 0;
        push(&queue, &copy);
        queue_pos = -1;
        return;
    }
    push(&queue, &copy);
    track last = queue.items[queue.len - 1];
    memmove(&queue.items[queue_pos + 2], &queue.items[queue_pos + 1], (size_t)(queue.len - queue_pos - 2) * sizeof last);
    queue.items[queue_pos + 1] = last;
}

bool lib_next(void) {
    if (queue_pos + 1 >= queue.len) return false;
    queue_pos++;
    return true;
}

bool lib_prev(void) {
    if (queue_pos <= 0) return false;
    queue_pos--;
    return true;
}

bool lib_has_next(void) { return queue_pos + 1 < queue.len; }
bool lib_has_prev(void) { return queue_pos > 0; }
bool lib_has_current(void) { return current() != NULL; }

void lib_now_playing(double duration, double position, bool playing) {
    static char key[1024], title[256];
    static double last_duration = -1, last_position, last_time;
    static bool last_playing, last_has;
    track *t = current();
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    double expected = last_position + (last_playing ? now - last_time : 0);
    bool same = last_has == (t != NULL) && playing == last_playing && duration == last_duration &&
                (!t || (!strcmp(key, t->key) && !strcmp(title, t->title))) && fabs(position - expected) < 1.5;
    if (same) return;
    last_has = t != NULL;
    last_playing = playing;
    last_duration = duration;
    last_position = position;
    last_time = now;
    if (t) {
        snprintf(key, sizeof key, "%s", t->key);
        snprintf(title, sizeof title, "%s", t->title);
    }
    np_update(t, duration, position, playing);
}

int32_t lib_playlists(void) { return playlist_count; }
int64_t lib_playlist_id(int32_t i) { return i >= 0 && i < playlist_count ? playlist_ids[i] : 0; }
int32_t lib_playlist_name(int32_t i, char out[256]) { return i >= 0 && i < playlist_count ? gs_copy(out, 256, playlist_names[i]) : 0; }

int64_t lib_playlist_new(void) {
    char name[64];
    snprintf(name, sizeof name, "Playlist %d", playlist_count + 1);
    sqlite3_stmt *s = prepare("INSERT INTO playlists(name) VALUES(?1)");
    sqlite3_bind_text(s, 1, name, -1, SQLITE_STATIC);
    sqlite3_step(s);
    sqlite3_finalize(s);
    int64_t id = sqlite3_last_insert_rowid(db);
    load_playlists();
    return id;
}

void lib_playlist_rename(int64_t id, gs_str name) {
    char n[128];
    gs_cstr(name, n, sizeof n);
    if (!n[0]) return;
    sqlite3_stmt *s = prepare("UPDATE playlists SET name = ?1 WHERE id = ?2");
    sqlite3_bind_text(s, 1, n, -1, SQLITE_STATIC);
    sqlite3_bind_int64(s, 2, id);
    sqlite3_step(s);
    sqlite3_finalize(s);
    load_playlists();
}

void lib_playlist_delete(int64_t id) {
    sqlite3_stmt *s = prepare("DELETE FROM playlist_items WHERE playlist = ?1");
    sqlite3_bind_int64(s, 1, id);
    sqlite3_step(s);
    sqlite3_finalize(s);
    s = prepare("DELETE FROM playlists WHERE id = ?1");
    sqlite3_bind_int64(s, 1, id);
    sqlite3_step(s);
    sqlite3_finalize(s);
    load_playlists();
}

void lib_playlist_add(int64_t id, int32_t i) {
    const track *t = row(i);
    if (!t) return;
    track copy = *t;
    save(&copy);
    sqlite3_stmt *s = prepare("INSERT INTO playlist_items(playlist, key) VALUES(?1, ?2)");
    sqlite3_bind_int64(s, 1, id);
    sqlite3_bind_text(s, 2, copy.key, -1, SQLITE_STATIC);
    sqlite3_step(s);
    sqlite3_finalize(s);
    if (view_kind == V_PLAYLIST && view_playlist == id) reload();
}

void lib_playlist_remove(int32_t i) {
    const track *t = row(i);
    if (!t || view_kind != V_PLAYLIST) return;
    sqlite3_stmt *s = prepare("DELETE FROM playlist_items WHERE id = ?1");
    sqlite3_bind_int64(s, 1, t->item);
    sqlite3_step(s);
    sqlite3_finalize(s);
    reload();
}

void lib_add_folder(gs_str path) {
    char p[1024];
    gs_cstr(path, p, sizeof p);
    size_t n = strlen(p);
    while (n > 1 && p[n - 1] == '/') p[--n] = 0;
    sqlite3_stmt *s = prepare("INSERT OR IGNORE INTO folders VALUES(?1)");
    sqlite3_bind_text(s, 1, p, -1, SQLITE_STATIC);
    sqlite3_step(s);
    sqlite3_finalize(s);
    scan_start(db_path, p, cache_dir);
}

void lib_rescan(void) {
    sqlite3_stmt *s = prepare("SELECT path FROM folders");
    while (sqlite3_step(s) == SQLITE_ROW) scan_start(db_path, (const char *)sqlite3_column_text(s, 0), cache_dir);
    sqlite3_finalize(s);
}

int32_t lib_folders(void) {
    sqlite3_stmt *s = prepare("SELECT COUNT(*) FROM folders");
    int n = sqlite3_step(s) == SQLITE_ROW ? sqlite3_column_int(s, 0) : 0;
    sqlite3_finalize(s);
    return n;
}

bool lib_pick_folder(void) {
    char p[1024];
    if (!platform_pick_folder(p, sizeof p)) return false;
    gs_str s = { (const uint8_t *)p, (int64_t)strlen(p) };
    lib_add_folder(s);
    return true;
}

bool lib_is_dir(gs_str path) {
    char p[1024];
    struct stat st;
    return stat(gs_cstr(path, p, sizeof p), &st) == 0 && S_ISDIR(st.st_mode);
}

static double np_last;
int32_t lib_np_command(void) { return np_command(&np_last); }
double lib_np_value(void) { return np_last; }
