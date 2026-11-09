#include "../games/totk/third_party/d2r_gfx/gfx/nvn/mask_skip.h"
#include <stdio.h>
#include <string.h>

static int add_key(mask_skip_set *s, const char *text) {
    return mask_skip_add(s->key, &s->nkey, MASK_SKIP_KEYS, text, 16);
}

int main(void) {
    mask_skip_set s;
    memset(&s, 0, sizeof s);
    if (!add_key(&s, "6baeb6b6f3f234bc") || !add_key(&s, "861591DA36BDAB14")) return 1;
    if (!mask_skip_match(&s, "6baeb6b6f3f234bc") || !mask_skip_match(&s, "861591da36bdab14")) return 2;
    if (mask_skip_match(&s, "91aba0d5c9ffc631")) return 3;
    mask_skip_del(s.key, &s.nkey, "6BAEB6B6F3F234BC", 16);
    if (mask_skip_match(&s, "6baeb6b6f3f234bc") || !mask_skip_match(&s, "861591da36bdab14")) return 4;
    if (!mask_skip_add(s.pre, &s.npre, MASK_SKIP_PRE, "91AB", 4)) return 5;
    if (!mask_skip_match(&s, "91aba0d5c9ffc631") || mask_skip_match(&s, "91bba0d5c9ffc631")) return 6;
    memset(&s, 0, sizeof s);
    const char *seen[] = {"aaaaaaaaaaaaaaaa", "bbbbbbbbbbbbbbbb", "cccccccccccccccc", "dddddddddddddddd"};
    for (int i = 0; i < 4; i++) if (!mask_skip_add(s.seen, &s.nseen, MASK_SEEN_MAX, seen[i], 16)) return 7;
    s.mode = 1;
    if (!mask_skip_match(&s, "cccccccccccccccc")) return 8;
    if (!mask_skip_add(s.keep, &s.nkeep, MASK_SKIP_KEEP, "cccccccccccccccc", 16)) return 9;
    if (mask_skip_match(&s, "cccccccccccccccc") || !mask_skip_match(&s, "aaaaaaaaaaaaaaaa")) return 10;
    s.mode = 2;
    if (!mask_skip_match(&s, "bbbbbbbbbbbbbbbb") || mask_skip_match(&s, "dddddddddddddddd")) return 11;
    s.mode = 3;
    if (mask_skip_match(&s, "aaaaaaaaaaaaaaaa") || mask_skip_match(&s, "cccccccccccccccc")) return 12;
    if (!mask_skip_match(&s, "dddddddddddddddd")) return 13;
    if (!add_key(&s, "cccccccccccccccc") || !mask_skip_match(&s, "cccccccccccccccc")) return 14;
    if (!mask_io_interesting(1920, 1440) || !mask_io_interesting(320, 180) || !mask_io_interesting(120, 67)) return 15;
    if (mask_io_interesting(1920, 1080) || mask_io_interesting(512, 512)) return 16;
    memset(&s, 0, sizeof s);
    for (int i = 0; i < MASK_SKIP_KEYS; i++) {
        char key[17];
        snprintf(key, sizeof key, "%016x", i);
        if (!add_key(&s, key)) return 17;
    }
    if (add_key(&s, "ffffffffffffffff")) return 18;
    if (!mask_skip_hex("6baeb6b6f3f234bc", 16) || !mask_skip_hex("6bae", 4)) return 19;
    if (mask_skip_hex("6bae", 0) || mask_skip_hex("gggg", 4) || mask_skip_hex("0123456789abcdef0", 17)) return 20;
    printf("mask skip checks passed\n");
    return 0;
}
