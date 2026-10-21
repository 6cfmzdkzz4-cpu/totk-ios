# Reproducing controller feedback tables

The controller-feedback runtime uses four headers derived from a player's own verified base-1.0.0 RomFS. They are not source files and are not distributed:

- `gameplay_audio_fingerprints.h`
- `gameplay_extra_audio.h`
- `gameplay_haptic_patterns.h`
- `gameplay_dialogue_flows.h`

Use Python 3.14 or newer. Its standard-library `compression.zstd` module reads the game's dictionary-compressed resources. The generator fetches the structured EventFlow parser at the exact commit pinned by this repository into the work tree's ignored `tooling/feedback` directory. It verifies both the Git commit and the Python source-tree hash, and does not modify global or user Python packages. `tools/requirements-feedback.txt` records the same pin for isolated developer environments.

After the normal extraction and integrity checks have produced a RomFS directory containing `index.json` and `romfs.bin`, generate all four headers into ignored work directories:

```sh
python3.14 tools/prepare_feedback.py \
  --romfs /path/to/work/totk/extracted/v0/romfs \
  --output-dir /path/to/work/totk/build \
  --report-dir /path/to/work/totk/analysis/feedback
```

An existing checkout of the exact pinned EVFL revision can be used without installing it:

```sh
python3.14 tools/prepare_feedback.py \
  --romfs /path/to/work/totk/extracted/v0/romfs \
  --output-dir /path/to/work/totk/build \
  --report-dir /path/to/work/totk/analysis/feedback \
  --evfl-source /path/to/evfl
```

`prepare_feedback.generate_all(romfs, output_dir, report_dir, evfl_source=None, tooling_dir=None)` is the equivalent Python API. Add `tools/` to `sys.path` before importing it when calling from another build script. `tooling_dir` is optional; without it, the function derives the private tooling directory from the normal `extracted/v0/romfs` work-tree layout.

Generation fails closed unless all of these identities match:

- the verified base-1.0.0 RomFS digest recorded by the extraction index;
- the exact verified base-1.0.0 `rewritten/v0/main.patched.bin` digest used by the runtime ABI;
- the compressed and decompressed hashes of `RSDB/EventSetting.Product.100.rstbl.byml.zs`;
- the pinned EVFL commit and the SHA-256 of its Python source tree;
- the accepted SHA-256 of every generated header.

The dialogue allowlist is rebuilt directly from little-endian BYML v7 `EventSetting` rows and structured `Npc_*` EventFlow actions. It does not consume a copied evidence JSON. Audio fingerprints are admitted only after the complete standalone BWAV and BARS collision scans used by the runtime policy.

The report directory contains game-derived cue and event names and therefore belongs in ignored work output too. The generator writes destination files only after all four staged headers reproduce their accepted hashes. It never writes keys, decoded waveforms, EventFlow resources, or RomFS bytes into the source tree.
