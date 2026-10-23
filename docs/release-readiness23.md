# Personal iPad release scope: accepted build 23

Build 23 is a tested personal iPad candidate. Its source, signed packages and
acceptance evidence are distinct from subsequent export-name changes. Neither
the accepted packages nor the frozen build-23 source was changed for naming.

A subsequent failure-path audit reproduced three persistence defects in this
historical backend: failed committed journals could still upload, and failed
state/account-pin writes did not propagate. Build 23 and metadata-only build 24
remain preserved baselines. The narrow source repair and synthetic recovery
checks are described in [cloud durability](cloud-durability.md); a replacement
package needs its own compile, signing, integrity and device validation before
this release gate is closed.

## Application name

Both accepted build-23 variants already contain exactly **Tears of the Kingdom**
in `CFBundleDisplayName` and `CFBundleName`, including the spaces. Neither has an
`InfoPlist.strings` override. The source project resolves both fields from
`TOTK_APP_NAME`; the builders already default that display name correctly.

Future personal exports use `Tears of the Kingdom.app` as the default filename
and as the app folder inside an IPA. The supported builder defaults to
`dist/Tears of the Kingdom.app`. An explicitly supplied `--out` path is still
honored, including custom filenames. Quote paths containing spaces in a shell.
The low-level `--app-name` option still controls the display-name fields.
Choose the intended update version with the portable builder's `--build-number`;
an accepted package does not automatically advance the source CLI default.

The executable and internal Xcode product remain `TOTK`. A short internal target
or executable name does not set the visible application title. Changing an
export folder does not change bundle identity, save location, display profile,
controller tuning or cloud configuration. A newly packaged or renamed app still
requires its own final signing and package verification.

## Evidence for build 23

- Both iPhone and iPad Release packages compiled, signed and passed strict
  signature checks. Each preserved all nine embedded guest-code sections and
  all 76,842 game-data files, including 19,892 Metal libraries.
- Source validation passed 194 receiving UIKit checks, 718 FPS/counter checks
  both normally and under ASan/UBSan, 120 Python tests, nine policy programs and
  six strict iOS syntax checks. The publication audit passed. Host UIKit tests
  used synthetic touches and a stubbed game/cloud runtime.
- The installation coordinator reported successful iPad installation with all
  34 protected save files and cloud/preferences bytes preserved. The tester
  accepted the FPS hide/reveal interaction. The coordinator also reported
  3,005 device-gyro samples, 3,130 guest reads, zero sensor errors and a new
  acknowledged cloud sync. Those runtime results are coordinator observations;
  they are not additional device operations performed by this source audit.
- The signed iPhone-23 candidate is retained. Its installation and physical
  acceptance are deferred until the user requests them.

## Concrete release limits

- This is a personal development-signed build with a Development CloudKit
  container, increased-memory capability and a provisioning-profile device list.
  It is not an App Store, TestFlight or general public binary release. Provisioning
  expires and must be checked for each recipient and re-signing operation.
- Personal outputs contain the supplied game and derived assets. Only authored
  source/tooling belongs in the public repository; keys, game bytes, saves,
  profiles, certificates and complete apps remain private.
- The customized portable entry point supports the verified base-1.0.0 inputs
  and tested 4:3 / 239:110 display profiles. It does not certify other game
  versions, screen profiles or Apple TV.
- Accepted iPad interactions and gyro counters do not establish whole-game
  completion, every haptic scenario, calibrated gyro feel, sustained frame rate,
  long-session memory behavior or hardware frame-generation performance. These
  require the corresponding gameplay evidence rather than a source-test pass.
- The fresh iPad cloud acknowledgement is not a build-23 iPad/iPhone roundtrip.
  The phone is deliberately deferred. Conflict/offline/account-change and
  background-expiration behavior are separate scenarios; do not infer them
  from one successful save upload.

The coordinator owns any later metadata-only package clone, re-signing,
installation and final displayed-name check. The source naming audit performs
no game compilation, device operation, commit, push or publication.
