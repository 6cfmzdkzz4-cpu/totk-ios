/* Opt-in RomFS mod directory check. It does not read the game image or hashes.
   The runtime overlay still checks each file against the base 1.0.0 RomFS. */
#pragma once
#include <stddef.h>

enum {
    ROMFS_MODS_OFF = 0,
    ROMFS_MODS_READY = 1,
    ROMFS_MODS_REJECTED = 2
};

/* enabled == 0, or a missing directory/manifest, returns OFF.
   A symlink, a path that leaves the directory, or an executable/cheat name
   returns REJECTED and leaves the caller's game files unused.
   READY means every listed file is a regular file inside directory. */
int romfs_mods_inspect(const char *directory, int enabled, char *reason, size_t reason_size);
