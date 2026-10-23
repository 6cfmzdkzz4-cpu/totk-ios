# iOS frame-rate counter

The small top-left counter uses the shared `DmFPSOverlay` UIKit control. A tap
hides its label; the same transparent corner remains available to reveal it.
Visibility is stored locally as `DmFPSOverlayHidden`. VoiceOver supports the same
toggle. This does not change touch-pad ownership or add a settings row.

The normal target is 80 by 22 points, four points inside the safe left edge and
two below the safe top edge. At short landscape heights its width is capped to
leave the L3 center reachable. The exact corner takes priority over the expanded
upper ZL hit rim; other touches pass through. The control remains interactive
when the gameplay touch controls are hidden for a physical controller.

`fps_overlay_note_displayed` runs from the existing Metal drawable presentation
callback. Only finite positive `presentedTime` values increment the total; the
generated counter is a subset of that total. The HUD samples the two monotonic
counters once per second and marks intervals containing generated pictures with
`FG`. A long pause or counter reset clears stale readings. Existing submitted
game-ticket and generated-work rate meters remain separate.

On iOS only the older GPU FPS text is suppressed. Shader and system-load HUD
layers keep their existing behavior. Mac GPU text is unchanged.

The FPS probe checks failed timestamps, native/generated separation, concurrent
presentation callbacks and preservation of legacy rate meters. Receiving UIKit
checks must also cover both landscape orientations, held gameplay touches,
controller-hidden controls, persistence and the invisible reveal target.
