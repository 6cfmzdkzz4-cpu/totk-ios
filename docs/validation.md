# Validation scope

The customized iPad build 18 was installed before this source-publication task.
Its full-screen 1920 × 1440 rendering, DualSense discovery, gameplay observer and
early full-travel bow-trigger curve were observed. All 31 synchronized save files
were preserved during that update. The user subsequently reported “It works
great.” Earlier phone builds verified the 1920 × 1080 canvas, and a new committed
save was independently verified across both directions of the iCloud handoff.

Those observations apply to the earlier personal builds, not automatically to
every fresh build, device or Apple account. The publication checkout uses the
same gameplay policies but replaces personal build/signing assumptions with
portable tooling. Its source checks, generated-asset comparison and fresh build
results are recorded in `docs/publication/validation.json`. Fresh private iPad and
iPhone apps were assembled and their complete packaged RomFS, shader manifests,
settings and entitlements were read back. These were unprovisioned local builds;
no personal signing or live CloudKit transaction was requested.

The public CI runs only synthetic, game-data-free tests and source audits. It
cannot certify full gameplay, frame rate, physical controller feel, signing
capability availability or live CloudKit sync. No device app is replaced as part
of publishing this repository.

The 4:3 and 239:110 display profiles were tuned for the tested devices; other
screens may need a new verified profile. Memory pressure, new shader variants,
long sessions and untested areas can still expose problems. Apple TV is not a
supported target in this repository.
