# Supported personal inputs

The customized iPad/iPhone build supports base **1.0.0**, title
`0100F2C0115B6000`, from an uncompressed NSP or XCI. It does not accept updates,
combined packages or compressed base dumps through `tools/ios_build.py`.

`games/totk/assets/expected-v1.0.0.json` pins the original ExeFS modules,
rewritten modules and complete RomFS. The importer reads the container header,
checks the title, decrypts using the caller's local keys, verifies source identity
and hashes, then rewrites the code and checks the resulting bytes. Cached RomFS
bytes are rehashed by the supported driver; a stale index is insufficient.

The local base NSP had a modified `main.npdm` permission-metadata file with digest
`e78c40978c23b42866973e6fe506c2ad7716ec51c651f35bfeada3415bb3b634`.
Its Nintendo ACID signature is **invalid**. That one exact digest is accepted only
when the source container binding, metadata structure/title ID, and every expected
original executable digest match. The native host neither reads nor packages
NPDM. The exception does not accept arbitrary unsigned metadata or certify the
container's signature, and never changes executable, rewrite or RomFS checks.
Unrecognized metadata is still subject to the original hash/ACID checks.

NSP files lack system fonts. Supply your own unpacked firmware NCA directory or
six previously extracted BFTTF fonts. Reference fonts use pinned hashes; another
valid BFTTF is checked structurally and bound by its actual bytes. XCI can supply
fonts from its system partition where present. No game, keys or firmware are
retrieved from the network.

`tools/prepare_feedback.py` reproduces four private headers from the verified
RomFS. It derives exact guard/melee/audio identities, sound-shaped envelopes and
NPC talk events from structured assets, using a pinned EVFL parser. Generated
headers are hash-locked to the reviewed base-1.0.0 outputs and stay in `work/`.
The display tool checks authored helper/template identities before preparing
local camera/HUD overlays. All resulting game content stays private.

The public shader list contains hashes and numeric binding/pipeline metadata,
not shader instructions or compiled libraries. The source-built translator reads
actual shader bytes only from the user's local game and prepares the matching
variants locally. No game-derived MSL, SPIR-V or metallibs are published.

## Build dependencies

SPIRV-Cross is fetched by the upstream full commit pin; modified cached source is
rejected. The iOS codec builds pin Opus 1.6.1 at
`22244de5a79bd1d6d623c32e72bf1954b56235be` and libvpx 1.16.0 at
`1024874c5919305883187e2953de8fcb4c3d7fa6`. Each reused checkout must still
match its exact commit and have a clean source tree. The private dependency
manifest records those commits, SDK and static-library hashes. Compiler outputs
can vary with Xcode/SDK, paths and NativeAOT version; local receipts bind their
actual bytes rather than claiming cross-toolchain byte identity.

A reusable device shader seed must match the source manifest, Metal math mode,
required library set and every recorded file hash. Extra files, symlinks or
changed bytes invalidate the cache before a new app manifest is written.
