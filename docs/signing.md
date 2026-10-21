# Personal signing and private iCloud saves

Build with a stable reverse-domain bundle ID you control. Add your Apple account
in Xcode Settings → Accounts and select its development team. Pair the intended
device, enable Developer Mode, and keep it unlocked for installation. Find its
identifier with `xcrun devicectl list devices`.

The supported builder can produce an unsigned personal IPA, or sign a prepared
app when both `--team` and `--device` are supplied. Its signing helper requests
increased memory and, only when configured, the caller's exact CloudKit
Development container. It obtains a development profile through Xcode and checks
its platform, application ID, team, device coverage, expiry, permitted
entitlements and matching local development certificate before changing the app.
It then signs the complete bundle and verifies the final signature and
entitlements. `--install` is a separate explicit choice.

## CloudKit setup

1. In your Apple developer account, create an explicit App ID matching your
   chosen bundle ID and enable iCloud/CloudKit. Create or select a private iCloud
   container such as `iCloud.com.yourname.totk` and associate it with that App ID.
   The account must permit this capability and increased memory.
2. Build with `--cloud-container iCloud.com.yourname.totk --team YOURTEAMID
   --device YOUR_DEVICE_UDID`. This writes the same container into the app's
   Info.plist and requested Development entitlements. The signer rejects a
   namespace mismatch or missing capability; no fallback disables required
   profile validation.
3. Sign the iPad and iPhone builds with the same Apple team, bundle ID and
   container. Use the same iCloud account on both devices and enable iCloud Drive
   in device Settings. These personal builds use the **Development** CloudKit
   environment. A separate team or Production environment cannot see that data.
4. Save in the game and fully close it before switching devices. The next device
   checks the cloud before starting. Committed saves upload automatically in the
   background; a concurrent divergent save prompts for a choice and preserves
   backups. A failed upload leaves local progress available for retry.

Without a cloud container, the game starts immediately with local saves and no
CloudKit operations. Runtime save location is `Documents/save`. It remains
available through Files/Finder; sync backups are under `Documents/iCloudSync`.
Changing the bundle ID or deleting the app can detach/remove that local data.

The signing helper may create/register a development App ID or refresh a
provisioning profile only when you explicitly invoke signing. This repository
ships no account tokens, signing identity, device list or provisioning profile.
All personal signing outputs and receipts stay in the ignored work directory.
If Xcode cannot authorize a capability, use its account/capability UI to resolve
the error; do not force unsupported entitlements into the signature.

A game-free on-device importer is not included: rewritten code must be embedded
before the complete app is signed. Every `.app` or `.ipa` built from your game
contains its data and must stay private.
