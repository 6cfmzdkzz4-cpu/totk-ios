# Cloud-save durability and retry gates

The coordinator must retain a complete committed snapshot before cloud work.
A failed `last-committed.plist` write leaves the newest snapshot available in
memory for retry, but cannot permit an upload or a successful acknowledgement.
The normal startup, foreground/background poll and conflict-choice paths pass
through the same journal gate. Local game progress remains in `Documents/save`.

One lock serializes local journal writes and snapshot publication. It is never
held during CloudKit waits. This prevents an older retry from replacing a newer
commit's journal. The serial sync queue continues to choose the newest published
snapshot and retains the existing retry schedule and preboot-only import rule.

Account pins and acknowledged base digests are prepared in a separate candidate
state. The in-memory state advances only after its atomic plist write succeeds.
A failed write returns an error; it cannot report `Synced with iCloud`. If the
server has already confirmed an upload, its candidate ancestry waits in memory
until state persistence succeeds. This happens before reconciling a newer save,
so the app's own confirmed upload is not mistaken for remote divergence.

These changes preserve the existing snapshot format, record identity, account
matching, CloudKit change-tag save policy, atomic restore/backups, timeouts and
lifecycle behavior. They do not claim power-loss testing or a new cross-device
handoff. If the process exits before pending ancestry can be persisted, normal
startup reconciliation and conflict preservation still apply.

Run `python3 games/totk/tests/cloud_coordinator/run.py` on macOS. The test uses
the real coordinator, archive codec and Foundation file operations with complete
authored synthetic saves. File/directory collisions cause real write errors.
It checks all three failure classes and recovery, pending acknowledgements,
account changes, commits during upload, CAS failures, conflict backups and startup
imports. UIKit and CloudKit service callbacks are test substitutes; no user
Documents, game bytes, network service, signing identity or device is accessed.

Use `--out /private/test-output` to retain evidence and `--sanitize undefined`
or `--sanitize address,undefined` for host sanitizers. This compiles only the
small host fixture, not a game app.
