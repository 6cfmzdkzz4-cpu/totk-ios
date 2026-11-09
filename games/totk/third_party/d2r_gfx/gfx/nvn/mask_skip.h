#ifndef TOTK_MASK_SKIP_H
#define TOTK_MASK_SKIP_H
/* Diagnostic fragment-key skip. Explicit keys and prefixes always match. A census mode matches
   keys remembered from the last mask_draw frame, except keys on the keep list. */
#include <string.h>
#define MASK_SKIP_KEYS 16
#define MASK_SKIP_PRE 4
#define MASK_SKIP_KEEP 8
#define MASK_SEEN_MAX 64

typedef struct {
    char key[MASK_SKIP_KEYS][17];
    int nkey;
    char pre[MASK_SKIP_PRE][17];
    int npre;
    char keep[MASK_SKIP_KEEP][17];
    int nkeep;
    char seen[MASK_SEEN_MAX][17];
    int nseen;
    int mode; /* 0 off, 1 every seen key, 2 first half, 3 second half, 4 first `limit` keys */
    int limit;
} mask_skip_set;

static char mask_skip_lower(char c) {
    if (c >= 'A' && c <= 'F') return (char)(c - 'A' + 'a');
    return c;
}
static int mask_skip_hex(const char *s, int n) {
    if (!s || n <= 0 || n > 16) return 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return 0;
    }
    return 1;
}
static int mask_skip_eq(const char *stored, const char *text, int len) {
    for (int i = 0; i < len; i++) if (stored[i] != mask_skip_lower(text[i])) return 0;
    return stored[len] == 0;
}
static int mask_skip_find(char list[][17], int n, const char *text, int len) {
    for (int i = 0; i < n; i++) if (mask_skip_eq(list[i], text, len)) return i;
    return -1;
}
/* 1 when the entry is present afterwards. 0 when the list was already full. */
static int mask_skip_add(char list[][17], int *n, int cap, const char *text, int len) {
    if (mask_skip_find(list, *n, text, len) >= 0) return 1;
    if (*n >= cap) return 0;
    for (int i = 0; i < len; i++) list[*n][i] = mask_skip_lower(text[i]);
    list[*n][len] = 0;
    (*n)++;
    return 1;
}
static void mask_skip_del(char list[][17], int *n, const char *text, int len) {
    int i = mask_skip_find(list, *n, text, len);
    if (i < 0) return;
    for (int j = i; j + 1 < *n; j++) memcpy(list[j], list[j + 1], 17);
    (*n)--;
}
static int mask_skip_match(const mask_skip_set *s, const char *key) {
    if (!s || !key || !key[0]) return 0;
    for (int i = 0; i < s->nkey; i++) if (!strncmp(key, s->key[i], 16)) return 1;
    for (int i = 0; i < s->npre; i++) {
        size_t n = strlen(s->pre[i]);
        if (n && !strncmp(key, s->pre[i], n)) return 1;
    }
    if (!s->mode || s->nseen <= 0) return 0;
    int half = s->nseen / 2;
    int lo = s->mode == 3 ? half : 0;
    int hi = s->mode == 2 ? half : s->mode == 4 ? (s->limit < s->nseen ? s->limit : s->nseen) : s->nseen;
    for (int i = lo; i < hi; i++) {
        if (strncmp(key, s->seen[i], 16)) continue;
        for (int k = 0; k < s->nkeep; k++) if (!strncmp(key, s->keep[k], 16)) return 0;
        return 1;
    }
    return 0;
}
static int mask_io_interesting(unsigned w, unsigned h) {
    return (w == 1920 && h == 1440) || (w == 320 && h == 180) || (w == 120 && h == 67);
}
#endif
