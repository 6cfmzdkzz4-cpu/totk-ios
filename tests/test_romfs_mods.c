#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif
#define _GNU_SOURCE 1
#include "../games/totk/runtime/romfs_mods.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(x) do { assert(x); checks++; } while (0)

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(text, f);
    fclose(f);
}

int main(void) {
    char dir[] = "/tmp/totk-romfs-mods-XXXXXX";
    assert(mkdtemp(dir));
    char reason[128];
    CHECK(romfs_mods_inspect(dir, 0, reason, sizeof reason) == ROMFS_MODS_OFF);
    CHECK(romfs_mods_inspect(dir, 1, reason, sizeof reason) == ROMFS_MODS_OFF);

    char manifest[512], payload[512], outside[] = "/tmp/totk-romfs-mods-out-XXXXXX";
    snprintf(manifest, sizeof manifest, "%s/manifest.tsv", dir);
    snprintf(payload, sizeof payload, "%s/UI/Pack/Example.bntx", dir);
    assert(mkdtemp(outside));
    char ui[512], nested[512];
    snprintf(ui, sizeof ui, "%s/UI", dir);
    snprintf(nested, sizeof nested, "%s/UI/Pack", dir);
    assert(mkdir(ui, 0755) == 0);
    assert(mkdir(nested, 0755) == 0);
    write_file(payload, "mod-bytes");
    write_file(manifest, "# game-version v0\nUI/Pack/Example.bntx\t9\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\t-\n");
    CHECK(romfs_mods_inspect(dir, 0, reason, sizeof reason) == ROMFS_MODS_OFF);
    CHECK(strcmp(reason, "disabled") == 0);
    CHECK(romfs_mods_inspect(dir, 1, reason, sizeof reason) == ROMFS_MODS_READY);
    write_file(manifest, "# game-version v143\nUI/Pack/Example.bntx\t9\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\t-\n");
    CHECK(romfs_mods_inspect(dir, 1, reason, sizeof reason) == ROMFS_MODS_REJECTED);
    CHECK(strstr(reason, "1.0.0"));
    write_file(manifest, "# game-version v0\nUI/Pack/Example.bntx\t9\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\t-\n");

    write_file(manifest, "../secret\t1\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\t-\n");
    CHECK(romfs_mods_inspect(dir, 1, reason, sizeof reason) == ROMFS_MODS_REJECTED);

    write_file(manifest, "UI/Pack/Example.pchtxt\t9\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\t-\n");
    CHECK(romfs_mods_inspect(dir, 1, reason, sizeof reason) == ROMFS_MODS_REJECTED);

    char link[512]; snprintf(link, sizeof link, "%s/UI/Pack/Link.bntx", dir);
    assert(symlink(outside, link) == 0);
    write_file(manifest, "UI/Pack/Link.bntx\t9\t0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\t-\n");
    CHECK(romfs_mods_inspect(dir, 1, reason, sizeof reason) == ROMFS_MODS_REJECTED);
    CHECK(strstr(reason, "symlink"));

    printf("%u RomFS mod directory checks passed\n", checks);
    return 0;
}
