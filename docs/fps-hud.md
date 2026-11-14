# iOS frame-rate counter

The counter is only a display. The frame cap is a separate Settings control.

## Frame cap

Settings can pace presents at 30 or 60. 30 is the default. The host holds the
present period to that ceiling and the existing real-time frame step samples the
same spacing, so a 30 cap advances game time by about 1/30 second per present.
60 uses the existing present-interval patch, whose minimum interval is one 60 Hz
tick. Slow scenes can still drop to a longer interval. Neither value is a
measurement that the device sustains that rate.

**90 and 120 are unavailable.** They are shown in the in-app list so the limit
is visible, and they cannot be selected or saved. The game's present interval is
a whole 60 Hz tick and the existing patch floors it at 1. Driving that tick from
a 90 or 120 Hz clock would reinterpret the interval the game calculated.
Frame-counted interface logic was measured to diverge when presents were not
held to the 60 Hz model. The real-time step clamps a sample to at least 1/120
second so a faster present cannot make the game run fast; that clamp is not a
90 or 120 gameplay mode. This tree adds no instruction patch for those rates.

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
