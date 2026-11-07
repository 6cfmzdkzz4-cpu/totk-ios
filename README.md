# Tears of the Kingdom for iPad and iPhone

An experimental native Apple silicon port of **The Legend of Zelda: Tears of the Kingdom**, with full-screen display profiles, DualSense feedback, gyro aiming and optional background iCloud saves.

The game's ARM64 code runs directly on the device. A small host implements the Switch services it uses, and its NVN graphics are translated to Metal. No JIT or jailbreak is needed. Build your personal app on a Mac using your own game dump, keys and system fonts.

## What works

- iPad: full-screen 4:3, starting at **1920 × 1440**.
- iPhone: a **1920 × 1080** canvas fitted to the 239:110 display profile.
- iPad and Duo keep one verified 4:3 camera, viewport and HUD picture. A wider scene, including iPhone Duo landscape, keeps constant black side bars. A 16:9 pre-rendered cutscene is fitted inside that same picture. The wide-phone build keeps its separate 239:110 preparation. See [live geometry and evidence limits](docs/live-display.md).
- Touch controls and physical gamepads. DualSense adds separate handle haptics, motion aiming, light shield resistance and a bow curve that increases through the trigger pull.
- Contextual hits, landings, conversations, weapon charge, swimming, climbing and skydive textures. Exact audio-derived identities and envelopes are generated locally from the game.
- Optional CloudKit save handoff between your devices. Routine sync runs in the background after committed saves. Actual conflicting saves require a choice so progress is preserved.
- A top-left FPS counter: tap to hide it, then tap the same corner to show it again. The counter is separate from the frame cap in Settings.

## Limits

**Frame cap.** Settings can pace the game at **30** or **60**. 30 is the default and holds presents to 30 Hz; the existing real-time frame step follows that spacing. 60 uses the existing present-interval patch and is a ceiling, not evidence that a scene holds 60. **90 and 120 are unavailable** and cannot be selected. The game's present interval is a whole number of 60 Hz ticks and is floored at 1. Interface logic that counts frames was measured to diverge when presents leave that model. The real-time step's 1/120 second floor only keeps the game from running fast. It is not a 90 or 120 mode, and this change adds no gameplay patch for those rates.

**Display.** The stable 4:3 picture and the 16:9 cutscene fit are source-tested. This change has not been run on an iPhone Duo, or on any device.

**App icon.** `--app-icon` scales a local square JPEG or PNG into the iOS icon sizes. The image stays on your Mac. A 553 pixel source is upscaled for the 1024 pixel marketing icon. The generated art is not checked here.

The source derives from [MacNativeGames](https://github.com/ly05010419/MacNativeGames), pinned at `15b1afd6255ec777ebceee0c62d33ab90861b07e`. The customized base-1.0.0 iPad build was played with a DualSense and accepted by its tester. The iPhone display and two-way save handoff were also tested in earlier local builds. That is scoped device evidence, not whole-game or sustained frame-rate certification. See [validation](docs/validation.md) and [the accepted build-23 release scope](docs/release-readiness23.md).

## What you need

- An Apple silicon Mac on macOS 26 or newer, full Xcode 26 or newer with its Metal toolchain, and Homebrew.
- An iPad with an M-series chip or a compatible recent iPhone, on iPadOS/iOS 26 or newer. The tested iPad is a 13-inch M5 iPad Pro. Other displays can need a different aspect profile.
- Your **base game 1.0.0** (`0100F2C0115B6000`), as an uncompressed **NSP or XCI**, and your own `prod.keys`.
- For NSP input, an unpacked firmware NCA folder or previously extracted shared fonts. The build checks and binds their actual bytes.
- About **65 GiB free** for a fresh build and personal IPA. Direct `.app` output uses less space on APFS. A cold shader build can take a while; progress and private logs remain in `work/`.
- An Apple development account and a profile permitting the increased-memory entitlement. iCloud additionally needs a CloudKit container owned by your Apple team. Account capability availability is checked when signing.

This customized build path supports base **1.0.0**. It rejects other executable/rewrite/RomFS hashes; the filename does not select compatibility. A single known altered, unused `main.npdm` metadata digest is accepted only with the exact expected executable bytes. Its Nintendo signature is invalid and the metadata is not included in the app. [Input verification](docs/inputs.md) explains the boundary.

## Saves and iCloud

Local progress is in `save/` inside the app's Documents folder, available through Files or Finder. Keep the same bundle ID and signing team when updating. Back up that folder before deleting or replacing an app.

Without `--cloud-container`, the game starts with local saves. To enable private CloudKit sync, use your own exact container, such as `iCloud.com.yourname.totk`, and the same container, team, Development environment and Apple account on each device. Containers do not share data across Apple teams. Existing installations must retain their original identities to keep their saves and cloud history.

The sync code checks complete save commits, verifies snapshots, retains backups, and detects concurrent changes. Save and close one device before playing on another. [Signing and iCloud](docs/signing.md) covers setup and recovery.

## Building and contributing

The supported entry point is `tools/ios_build.py`. It imports and verifies the game, rewrites its three modules, regenerates private feedback/display data, builds the shader translator from source, prepares iOS shaders and builds a personal `.app` or `.ipa`. `--prepare-only` stops after verified import and asset preparation. Signing and installation occur only when explicitly requested with the corresponding flags.

The repository contains source, authored patch descriptions, hashes and shader/pipeline identifier metadata. Game programs, audio fingerprints, generated haptic tables, RomFS, compiled shaders, fonts, keys, saves, profiles and personal apps stay outside Git. There is no downloadable game-containing IPA or on-device game importer; this architecture embeds rewritten code in the signed executable.

Run source tests with a Python environment containing `games/totk/requirements.txt`:

```sh
python3.14 -m venv .venv
.venv/bin/python -m pip install -r games/totk/requirements.txt
.venv/bin/python -m unittest discover -s tests -v
python3 tools/test_feedback_policy.py
python3 games/totk/tests/cloud_coordinator/run.py
python3 tools/audit_publication.py --staged --history
```

Read [AGENTS.md](AGENTS.md) or [CLAUDE.md](CLAUDE.md) before automated changes. The [original upstream README](README.upstream.md) and [architecture notes](docs/how-it-works.md) are retained as background; their legacy commands and other game-version claims are not the supported customized build workflow.

## Credits and license

Built on [ly05010419/MacNativeGames](https://github.com/ly05010419/MacNativeGames), [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile), Ryujinx, SPIRV-Cross, deko3d, bcdec, libopus and libvpx. The display preparation uses [Fruithapje21/TOTK-Ultrawide](https://github.com/Fruithapje21/TOTK-Ultrawide), and dialogue preparation uses [zeldamods/evfl](https://github.com/zeldamods/evfl). See [third-party notices](THIRD_PARTY_NOTICES.md) and the licenses beside each component. Runtime and upstream host code retain MIT; the separately licensed display-preparation tool and TOTK-Ultrawide component are GPL-3.0-only.

Nintendo owns the game and its assets. This independent fan project is not affiliated with Nintendo. Every personal output app contains your game data and must remain private.

Related: [Super Mario Odyssey for iPad and iPhone](https://github.com/NicholasDominici/super-mario-odyssey-ios).

## Installing

1. **Install the tools on your Mac.** Install full Xcode, open it once, add your Apple account under **Xcode → Settings → Accounts**, and install the Metal toolchain under Xcode's Components settings. Install Homebrew if needed, then run:

   ```sh
   brew install python@3.14 opus libvpx xcodegen cmake ninja
   ```

   Install the **.NET 10 SDK for macOS Arm64** from [Microsoft](https://dotnet.microsoft.com/en-us/download/dotnet/10.0). It is needed to build the shader translator, not to play.

2. **Get the source and prepare your files.** Put your base-1.0.0 NSP/XCI, `prod.keys` and unpacked firmware in a private folder. Then run:

   ```sh
   git clone https://github.com/NicholasDominici/totk-ios.git
   cd totk-ios
   ```

3. **Build your personal iPad IPA.** Replace the three input paths and `com.yourname.totk` with your own values:

   ```sh
   python3.14 tools/ios_build.py "/path/to/TOTK.nsp" \
     --keys "/path/to/prod.keys" --firmware "/path/to/firmware" \
     --bundle-id com.yourname.totk --display ipad --out "dist/Tears of the Kingdom.ipa"
   ```

   Use `--display duo` (equivalent to `ipad`) for the stable 4:3 narrow profile, or `--display iphone` for a conventional wide iPhone. Both variants keep the same save identity; install only the matching variant. Add `--app-icon "/path/to/your-square-image.jpg"` to use your own image as the icon. Keep the bundle id of an existing install so its saves stay available. If you have already extracted the six BFTTF fonts, replace `--firmware` with `--shared-fonts "/path/to/shared-fonts"`. XCI dumps with the expected system partition can provide their own fonts. The command's `--help` lists the other options.

4. **Sign and install.** Sign `dist/Tears of the Kingdom.ipa` with your Apple account using a tool that preserves its increased-memory entitlement, such as [Sideloadly](https://sideloadly.io/). For direct Xcode-managed signing, connect and unlock the device, enable Developer Mode and trust the Mac, then add `--team YOURTEAMID --device YOUR_DEVICE_UDID --install` to step 3 and use `--out "dist/Tears of the Kingdom.app"`. `xcrun devicectl list devices` lists paired devices. The helper checks the profile before signing; it stops if a required capability is unavailable. For iCloud, first configure your container as described in [the signing guide](docs/signing.md), then also add `--cloud-container iCloud.com.yourname.totk` and use the direct signing route.

5. **Open Tears of the Kingdom.** Allow the first shader import to finish, then play. Pair your controller in the device's Bluetooth settings. Tap the on-screen gear for **Gyro**, **Device haptics** and **Also with controller**, then tap **Done** to apply changes. Existing resolution and controller tuning are retained during updates. Keep your chosen bundle ID for future builds and back up `save/` before removing the app.
