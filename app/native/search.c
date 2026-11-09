#include <ctype.h>
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <yyjson.h>
#include "native.h"

enum { MAX = 50 };

typedef struct {
    char id[32], title[256], artist[256], album[256], thumb[512];
    double duration;
} track;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static track results[MAX];
static int count, state, generation;

typedef struct { char *data; size_t len; } buffer;

static size_t collect(char *p, size_t size, size_t n, void *user) {
    buffer *b = user;
    char *grown = realloc(b->data, b->len + size * n + 1);
    if (!grown) return 0;
    b->data = grown;
    memcpy(b->data + b->len, p, size * n);
    b->len += size * n;
    b->data[b->len] = 0;
    return size * n;
}

static yyjson_val *path(yyjson_val *v, const char *keys) {
    char key[64];
    while (v && *keys) {
        const char *dot = strchr(keys, '.');
        size_t n = dot ? (size_t)(dot - keys) : strlen(keys);
        snprintf(key, sizeof key, "%.*s", (int)n, keys);
        v = yyjson_obj_get(v, key);
        keys += n + (dot ? 1 : 0);
    }
    return v;
}

static void runs(yyjson_val *text, char *out, size_t cap) {
    size_t len = 0;
    out[0] = 0;
    yyjson_val *run;
    size_t i, max;
    yyjson_arr_foreach(yyjson_obj_get(text, "runs"), i, max, run) {
        const char *t = yyjson_get_str(yyjson_obj_get(run, "text"));
        if (t) len += snprintf(out + len, len < cap ? cap - len : 0, "%s", t);
    }
    if (len >= cap) out[cap - 1] = 0;
}

static double duration(const char *s) {
    double total = 0;
    int parts = 0;
    while (*s) {
        if (!isdigit((unsigned char)*s)) return -1;
        double n = 0;
        while (isdigit((unsigned char)*s)) n = n * 10 + (*s++ - '0');
        total = total * 60 + n;
        parts++;
        if (*s == ':') s++;
        else if (*s) return -1;
    }
    return parts >= 2 && parts <= 3 ? total : -1;
}

static bool skip_credit(const char *t) {
    static const char *words[] = { "&", ",", "and", "Song", "Video", "Podcast", "Album", "Single", "EP", "Artist" };
    for (size_t i = 0; i < sizeof words / sizeof *words; i++)
        if (!strcmp(t, words[i])) return true;
    return duration(t) >= 0;
}

static bool parse_song(yyjson_val *item, track *t) {
    memset(t, 0, sizeof *t);
    yyjson_val *columns = yyjson_obj_get(item, "flexColumns");
    yyjson_val *first = path(yyjson_arr_get(columns, 0), "musicResponsiveListItemFlexColumnRenderer.text");
    runs(first, t->title, sizeof t->title);
    const char *id = yyjson_get_str(path(item, "playlistItemData.videoId"));
    if (!id) id = yyjson_get_str(path(yyjson_arr_get(yyjson_obj_get(first, "runs"), 0), "navigationEndpoint.watchEndpoint.videoId"));
    if (!id || !t->title[0]) return false;
    snprintf(t->id, sizeof t->id, "%s", id);
    yyjson_val *column;
    size_t c, cmax;
    yyjson_arr_foreach(columns, c, cmax, column) {
        if (c == 0) continue;
        bool credits = c == 1;
        yyjson_val *run;
        size_t r, rmax;
        yyjson_arr_foreach(path(column, "musicResponsiveListItemFlexColumnRenderer.text.runs"), r, rmax, run) {
            const char *text = yyjson_get_str(yyjson_obj_get(run, "text"));
            if (!text) continue;
            while (*text == ' ') text++;
            char name[256];
            snprintf(name, sizeof name, "%s", text);
            for (size_t n = strlen(name); n && name[n - 1] == ' '; n--) name[n - 1] = 0;
            const char *browse = yyjson_get_str(path(run, "navigationEndpoint.browseEndpoint.browseId"));
            double secs = duration(name);
            if (strstr(name, "\xE2\x80\xA2")) {
                if (t->artist[0]) credits = false;
            } else if (browse && !strncmp(browse, "MPRE", 4)) {
                snprintf(t->album, sizeof t->album, "%s", name);
                credits = false;
            } else if (secs >= 0) {
                t->duration = secs;
            } else if (credits && name[0] && !skip_credit(name) && !(browse && strncmp(browse, "UC", 2))) {
                size_t len = strlen(t->artist);
                snprintf(t->artist + len, sizeof t->artist - len, "%s%s", len ? ", " : "", name);
            }
        }
    }
    yyjson_val *thumbs = path(item, "thumbnail.musicThumbnailRenderer.thumbnail.thumbnails");
    const char *thumb = yyjson_get_str(yyjson_obj_get(yyjson_arr_get_last(thumbs), "url"));
    if (thumb) snprintf(t->thumb, sizeof t->thumb, "%s", thumb);
    return true;
}

static bool same(const char *a, const char *b) {
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) a++, b++;
    return !*a && !*b;
}

static int parse(const char *json, size_t len, const char *query, track *out) {
    yyjson_doc *doc = yyjson_read(json, len, 0);
    if (!doc) return -1;
    int n = 0;
    yyjson_val *tab, *section, *entry;
    size_t i, imax, j, jmax, k, kmax;
    yyjson_arr_foreach(path(yyjson_doc_get_root(doc), "contents.tabbedSearchResultsRenderer.tabs"), i, imax, tab) {
        yyjson_arr_foreach(path(tab, "tabRenderer.content.sectionListRenderer.contents"), j, jmax, section) {
            yyjson_val *shelf = yyjson_obj_get(section, "musicShelfRenderer");
            char title[64];
            runs(yyjson_obj_get(shelf, "title"), title, sizeof title);
            if (!shelf || strcmp(title, "Songs")) continue;
            yyjson_arr_foreach(yyjson_obj_get(shelf, "contents"), k, kmax, entry) {
                if (n < MAX && parse_song(yyjson_obj_get(entry, "musicResponsiveListItemRenderer"), &out[n])) n++;
            }
        }
    }
    yyjson_doc_free(doc);
    char wanted[256];
    snprintf(wanted, sizeof wanted, "%s", query);
    int front = 0;
    for (int m = 0; m < n; m++) {
        if (!same(out[m].artist, wanted)) continue;
        track hit = out[m];
        memmove(&out[front + 1], &out[front], (size_t)(m - front) * sizeof *out);
        out[front++] = hit;
    }
    return n;
}

typedef struct { int generation; char query[512]; } job;

static void *run(void *arg) {
    job *j = arg;
    yyjson_mut_doc *req = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(req);
    yyjson_mut_doc_set_root(req, root);
    yyjson_mut_val *context = yyjson_mut_obj_add_obj(req, root, "context");
    yyjson_mut_val *client = yyjson_mut_obj_add_obj(req, context, "client");
    yyjson_mut_obj_add_str(req, client, "clientName", "WEB_REMIX");
    yyjson_mut_obj_add_str(req, client, "clientVersion", "1.20261025.12.00");
    yyjson_mut_obj_add_str(req, client, "hl", "en");
    yyjson_mut_obj_add_str(req, client, "gl", "US");
    yyjson_mut_obj_add_str(req, root, "query", j->query);
    yyjson_mut_obj_add_str(req, root, "params", "EgWKAQIIAWoKEAMQBBAJEAoQBQ%3D%3D");
    char *body = yyjson_mut_write(req, 0, NULL);
    yyjson_mut_doc_free(req);

    buffer reply = { 0 };
    long status = 0;
    CURL *curl = curl_easy_init();
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Origin: https://music.youtube.com");
    curl_easy_setopt(curl, CURLOPT_URL, "https://music.youtube.com/youtubei/v1/search?prettyPrint=false");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply);
    CURLcode ok = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(body);

    static track found[MAX];
    static pthread_mutex_t parsing = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&parsing);
    int n = ok == CURLE_OK && status == 200 && reply.data ? parse(reply.data, reply.len, j->query, found) : -1;
    pthread_mutex_lock(&lock);
    if (j->generation == generation) {
        count = n < 0 ? 0 : n;
        if (n > 0) memcpy(results, found, (size_t)n * sizeof *found);
        state = n < 0 ? 3 : 2;
    }
    pthread_mutex_unlock(&lock);
    pthread_mutex_unlock(&parsing);
    free(reply.data);
    free(j);
    ui_wake();
    return NULL;
}

void search_init(void) { curl_global_init(CURL_GLOBAL_DEFAULT); }

void search_start(gs_str query) {
    job *j = malloc(sizeof *j);
    if (!j) return;
    gs_cstr(query, j->query, sizeof j->query);
    pthread_mutex_lock(&lock);
    j->generation = ++generation;
    state = 1;
    pthread_mutex_unlock(&lock);
    pthread_t thread;
    if (pthread_create(&thread, NULL, run, j)) {
        free(j);
        pthread_mutex_lock(&lock);
        state = 3;
        pthread_mutex_unlock(&lock);
        return;
    }
    pthread_detach(thread);
}

int32_t search_state(void) {
    pthread_mutex_lock(&lock);
    int s = state;
    pthread_mutex_unlock(&lock);
    return s;
}

int32_t search_count(void) {
    pthread_mutex_lock(&lock);
    int n = count;
    pthread_mutex_unlock(&lock);
    return n;
}

int32_t search_field(int32_t i, int32_t field, char out[256]) {
    pthread_mutex_lock(&lock);
    int32_t n = 0;
    if (i >= 0 && i < count) {
        const char *s = field == 0 ? results[i].title : field == 1 ? results[i].artist : field == 2 ? results[i].album : results[i].id;
        n = gs_copy(out, 256, s);
    }
    pthread_mutex_unlock(&lock);
    return n;
}

double search_duration(int32_t i) {
    pthread_mutex_lock(&lock);
    double d = i >= 0 && i < count ? results[i].duration : 0;
    pthread_mutex_unlock(&lock);
    return d;
}
