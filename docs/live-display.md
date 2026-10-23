# Live scene geometry

The base-1.0.0 iPad/Duo profile uses the verified 4:3 resource preparation and
adapts its camera, viewport and HUD together to landscape scene aspects from 1:1
through 16:9. Select `--display duo` or `--display ipad`; they prepare the same
narrow profile. The separate `--display iphone` preparation remains static
239:110 for conventional wide phones. A narrow build on an unsupported scene
shape explicitly returns to a centered 4:3 fit rather than retaining an old
camera ratio. The game view always preserves the completed source's proportions.

UIKit supplies the actual scene bounds, native display scale, safe areas and
orientation. It does not select a device identifier or assume a fold posture.
Invalid or zero intermediate sizes preserve the last valid request. The existing
internal render-resolution preference and pixel-height budget stay independent
of scene aspect. The narrow view fills the scene; immutable source metadata tells
the renderer how to fit a completed native frame or an explicit fallback.

The supported builder derives a private layout header from exact verified
executable and RomFS inputs, a pinned GPL display rule source and the prepared
Common archive. Generated resource identifiers and transforms stay in the private
work folder. Mac export prepares the exact Pane Calculate, eui Build, camera and
viewport veneers in the embedded signed text without serializing host callback
pointers. The iOS host verifies those instructions and binds only writable guest
data. A missing header or mismatched veneer fails closed. The wide build compiles
its metadata gate off even if a narrow header remains in a reused work folder.

Layout ownership binds resources by digest, pane kind and tree. Supported HUD
anchors include player status, minimap/environment/command, control hints and the
autosave icon. Each original Calculate consumes the current animated pose and
restores local fields after the guest has produced its global matrices. Native
capture islands and their Parts preserve their own coordinate domains. Unknown
visible geometry, an unqualified absolute matrix, wrong resource identity or
unsupported full-map mode rejects the whole completed source and requests the
prepared 4:3 fallback; these cases do not get a speculative transform.

The producer binds one completed camera/HUD generation to each native source.
The renderer carries that immutable identity through processing and presentation.
`mtl_present_geometry_changed()` advances an atomic epoch; the worker resets
MetalFX history at its frame boundary and skips old generated frames. UIKit takes
no graphics queue mutex, performs no GPU wait and does not reload the guest or
reopen saves. Geometry changes cancel touch ownership and pending gear taps;
physical controller state and its existing haptic lifecycle are preserved.

Evidence is scoped. CPU fixtures run exact private Pane/Calculate instructions,
native capture branches, current animation/safe anchors, fallback/recovery and the
code-export/read-only-text binding path. A continuous real Mac gameplay recording
was checked while actual Duo Simulator scene changes drove Open → Closed → Open,
including Link movement, camera control, jumping, hints and autosave. Its complete
UIKit-root frames preserved world/HUD proportions and a circular, safely anchored
minimap in both postures. This was Mac gameplay relayed into a UIKit viewer, not
game execution on Simulator or physical Duo hardware. All-game/all-menu behavior,
physical foldable performance and device controls in this new build remain
unverified. Device installation and testing are separate release steps.

Run the game-free geometry and profile regressions with `python3 -m unittest
discover -s tests -p 'test_ios_display_profiles.py' -v` and the corresponding
`test_live_display_geometry.py`. The private exact-code test is
`tests/test_live_layout_matrix.py --private-tree /path/to/verified/totk --paired`;
it disposes compiler products and never runs the game or touches saves/devices.
