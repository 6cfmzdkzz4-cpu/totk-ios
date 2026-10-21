# TOTK iOS contributor instructions

This repository is a source snapshot of MacNativeGames at the revision recorded in
`docs/publication/source-provenance.json`, with the customized base-1.0.0 iPad and
iPhone controller, display and optional CloudKit work. Read README.md and
`docs/inputs.md` before modifying build/import behavior.

- Use `tools/ios_build.py` for the supported personal build. Only the verified
  base-1.0.0 NSP/XCI path is supported here. Keep executable, rewrite, RomFS and
  source-identity gates. Never silently accept a new version or generic NPDM bypass.
- All game-derived bytes and generated headers belong in ignored private work
  folders. Do not commit games, keys, decrypted or rewritten modules, generated
  assembly/runtime tables, RomFS, shaders, fonts, ADPCM fingerprints, haptic tables,
  icons extracted from the game, saves, logs, profiles, certificates, apps or IPAs.
  Hashes and authored patch/identifier metadata are not raw game payloads.
- The feedback generators must reproduce their four pinned output hashes from
  the user's verified local game. Keep the EVFL source pin and parser-tree gate.
  Tests that need those generated headers run only against private local inputs.
- Preserve existing application identities and save data. Device installation,
  account/provisioning changes and CloudKit schema changes need explicit task
  authorization. Do not replace a working device app while publishing source.
- Routine CloudKit sync is silent. Preserve conflict resolution, snapshot
  verification, backups and account isolation. Use caller-owned container IDs.
- Controller effects must be bounded and quiet on pause, stale state, focus loss
  and disconnect. Do not infer verified gameplay events from a button alone.
- Preserve upstream and third-party attribution. The display preparation helper
  and TOTK-Ultrawide library are GPL-3.0-only; do not relabel them MIT.
- Run the Python source suite, pure feedback-policy tests, `git diff --check` and
  publication audit. For build changes, verify actual asset preparation and iOS
  compilation with private inputs. Build success is not live gameplay proof.
- Before publishing, audit the exact staged/committed tree and every commit being
  pushed. Never upload a personal build as a release artifact or CI artifact.
- Keep easy numbered installation directions at the very bottom of README.md,
  with no assistant dependency. Preserve unrelated work and commit/push only when
  the current task authorizes repository publication.
