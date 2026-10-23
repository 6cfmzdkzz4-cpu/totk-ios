# Shared touch controls and settings

The iOS overlay uses Odyssey's corner layout and the unchanged portable
`ios/Sources/dm_touch_controls.h` contract, version 5. The integration binds to
SHA-256 `566c05f748d80566522006036cee26dbdd842c3edade105f5c8da92a0b9fb944`.
The left stick is fixed, the camera stick is invisible and floating, the face
diamond uses Nintendo positions, and the D-pad sits inward below the left stick.
Shoulders, stick clicks, plus and minus retain their native Npad bits. Physical
GameController routing is unchanged. Full controls are always available,
including the D-pad and invisible floating right stick. Obsolete compact-layout
preferences remain stored but cannot hide controls.

The shared per-primitive palette uses 0.06 idle fill, 0.18 pressed fill/stroke,
0.32 text and 0.025 stick-ring fill. The overlay does not reduce full-screen
view opacity. A connected controller hides the gameplay controls while keeping
the settings gear reachable.

The top-center SF Symbol `gearshape` uses the shared 20-point regular style and
0.38 opacity. Its exclusive target is 44 points. A normal stationary tap opens
settings on release, with no timer or minimum hold. Touch-down never opens it.
A resting gameplay thumb may remain down; a second gear finger, travel beyond
12 points, leaving the target, cancellation, rotation, source changes or focus
loss invalidate the tap. Gear-owned touches remain consumed until release,
including after rejection. Coalesced movement is checked on both moves and
release, so a fast excursion and return cannot conceal excessive travel.
An explicit accessible **Open settings** action is also available.
The shared scale-aware plus/minus spacing leaves a five-point gap between
expanded button hit regions and the gear.

The panel contains exactly three native switches and Done: **Gyro**, **Device
haptics**, and **Also with controller**. Gyro Off disables motion; On selects
Auto, preferring fresh controller motion and falling back to device motion.
Old positive source preferences map internally to Auto without rewriting their
raw values. Device haptics retains the existing subtle level and is suppressed
with a physical controller unless the third switch is enabled. That switch
controls only the local actuator. Changes apply when the panel closes.
Unsupported local feedback rows are disabled and explained honestly.

The same three preference keys appear in the packaged Settings.bundle. New
boolean keys are initialized from legacy values only when absent. Existing raw
layout, source, device gain, controller/trigger and advanced display values are
preserved; they are not exposed as extra panel controls.

Presenting settings releases touch input and suspends the host through its
existing focus path. Scene foreground notifications respect that modal gate,
including a lock/background/return while the panel remains open. Dismissal
clears ownership again, so a finger that was already down cannot reacquire a
control by moving. A fresh touch-down is required.

Native SDK vibration transport remains available independently of controller
output mutes. This is necessary for live device feedback Off → On when both
controller sinks started disabled: the SDK discovers its handles at startup.
Guest-authored stops and explicit runtime permit/mute behavior are unchanged,
as are controller gain, adaptive triggers and local perceptual mapping.

`test_touch_controls.c` exercises the actual UIKit receiver's portable policy
across phone/iPad safe areas, both landscape directions, fixed/floating sticks,
button ownership, diagonals, configuration changes and deliberate settings
activation. `test_feedback_transport.c` calls the real SDK vibration bridge
under 24 sink combinations and checks live local enable plus native stop/mute.
These tests and compilation do not establish physical touch ergonomics or
haptic feel; those require the separately coordinated device session.

The overlay writes one coalesced atomic `touch-settings.json` snapshot on
settings/lifecycle events. It records bounded counters and current state rather
than touch coordinates or an unbounded event log. Host-only UIKit tests redirect
this file with `TOTK_TOUCH_DIAGNOSTICS_DIR`; production uses the app Documents
container. Game saves and cloud synchronization are separate.
