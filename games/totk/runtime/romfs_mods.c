/* Darwin hides O_NOFOLLOW when _POSIX_C_SOURCE is set without _DARWIN_C_SOURCE.
   _GNU_SOURCE selects the real flag on both Darwin and glibc. Do not define
   O_NOFOLLOW to 0: a missing flag must fail the build. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif
#define _GNU_SOURCE 1
#include "romfs_mods.h"
#include <stdint.h>
#include "romfs_path.h"
#include <errno.h>
#include <fcntl.h>
#ifndef O_NOFOLLOW
#error "O_NOFOLLOW is required so RomFS mods cannot follow symlinks"
#endif
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int explain(char *reason, size_t n, int status, const char *text) {
    if (reason && n) snprintf(reason, n, "%s", text ? text : "");
    return status;
}

static int open_regular(int root, const char *rel) {
    if (!romfs_path_valid_relative(rel)) return -1;
    int fd = -1, owned = 0;
    const char *part = rel;
    while (*part) {
        const char *slash = strchr(part, '/');
        size_t len = slash ? (size_t)(slash - part) : strlen(part);
        char name[256];
        if (!len || len >= sizeof name) { if (owned) close(fd); return -1; }
        memcpy(name, part, len);
        name[len] = 0;
        int next = openat(owned ? fd : root, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (owned) close(fd);
        if (next < 0) return -1;
        fd = next;
        owned = 1;
        struct stat st;
        if (fstat(fd, &st) != 0) { close(fd); return -1; }
        if (!slash) return S_ISREG(st.st_mode) ? fd : (close(fd), -1);
        if (!S_ISDIR(st.st_mode)) { close(fd); return -1; }
        part = slash + 1;
    }
    if (owned) close(fd);
    return -1;
}

static int unsupported_name(const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t n = strlen(base);
    static const char *suffix[] = {".npdm", ".nso", ".nro", ".nsp", ".xci", ".nsz", ".pchtxt", ".ips", ".elf"};
    for (unsigned i = 0; i < sizeof suffix / sizeof *suffix; i++) {
        size_t sn = strlen(suffix[i]);
        if (n > sn && !strcmp(base + n - sn, suffix[i])) return 1;
    }
    return 0;
}

int romfs_mods_inspect(const char *directory, int enabled, char *reason, size_t reason_size) {
    if (!enabled) return explain(reason, reason_size, ROMFS_MODS_OFF, "disabled");
    if (!directory || !*directory) return explain(reason, reason_size, ROMFS_MODS_REJECTED, "missing directory");
    int root = open(directory, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (root < 0) {
        if (errno == ENOENT) return explain(reason, reason_size, ROMFS_MODS_OFF, "no mods directory");
        return explain(reason, reason_size, ROMFS_MODS_REJECTED, errno == ELOOP ? "mods directory is a symlink" : "mods directory cannot be opened");
    }
    struct stat root_stat;
    if (fstat(root, &root_stat) != 0 || !S_ISDIR(root_stat.st_mode)) {
        close(root);
        return explain(reason, reason_size, ROMFS_MODS_REJECTED, "mods path is not a directory");
    }
    int manifest = openat(root, "manifest.tsv", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (manifest < 0) {
        close(root);
        if (errno == ENOENT) return explain(reason, reason_size, ROMFS_MODS_OFF, "no manifest");
        return explain(reason, reason_size, ROMFS_MODS_REJECTED, errno == ELOOP ? "manifest is a symlink" : "manifest cannot be opened");
    }
    struct stat manifest_stat;
    if (fstat(manifest, &manifest_stat) != 0 || !S_ISREG(manifest_stat.st_mode)) {
        close(manifest); close(root);
        return explain(reason, reason_size, ROMFS_MODS_REJECTED, "manifest is not a regular file");
    }
    FILE *file = fdopen(manifest, "r");
    if (!file) { close(manifest); close(root); return explain(reason, reason_size, ROMFS_MODS_REJECTED, "manifest cannot be read"); }
    char line[8192];
    unsigned files = 0;
    int saw_base = 0;
    int status = ROMFS_MODS_READY;
    const char *why = "ready";
    while (fgets(line, sizeof line, file)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            if (!strncmp(line, "# game-version ", 15)) {
                if (!strncmp(line, "# game-version v0", 17) && (line[17] == '\n' || line[17] == '\r' || line[17] == 0))
                    saw_base = 1;
                else { status = ROMFS_MODS_REJECTED; why = "manifest is not base 1.0.0"; break; }
            }
            continue;
        }
        char name[4096], sha[65], orig[65];
        unsigned long long size = 0;
        if (sscanf(line, "%4095[^\t]\t%llu\t%64[0-9a-f]\t%64[0-9a-f-]", name, &size, sha, orig) != 4
            || strlen(sha) != 64 || (strlen(orig) != 64 && strcmp(orig, "-"))) {
            status = ROMFS_MODS_REJECTED; why = "manifest line is malformed"; break;
        }
        if (!romfs_path_valid_relative(name) || unsupported_name(name)) {
            status = ROMFS_MODS_REJECTED; why = "manifest path is not a RomFS file"; break;
        }
        int fd = open_regular(root, name);
        if (fd < 0) { status = ROMFS_MODS_REJECTED; why = "mod file is missing or is a symlink"; break; }
        struct stat st;
        int bad = fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (uint64_t)st.st_size != size;
        close(fd);
        if (bad) { status = ROMFS_MODS_REJECTED; why = "mod file size does not match the manifest"; break; }
        files++;
    }
    fclose(file);
    close(root);
    if (status == ROMFS_MODS_READY && !saw_base) return explain(reason, reason_size, ROMFS_MODS_REJECTED, "manifest does not say base 1.0.0");
    if (status == ROMFS_MODS_READY && !files) return explain(reason, reason_size, ROMFS_MODS_REJECTED, "manifest lists no files");
    return explain(reason, reason_size, status, why);
}
