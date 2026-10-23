# Local device haptics

The iOS backend can feed the phone's own Core Haptics actuator from the same
native-rumble and verified semantic composition used by controller feedback.
This is a separate output sink with its own engine, settings, error generation
and lease. It adds no guessed gameplay events and changes no DualSense trigger
or handle policy.

Apple's [preparation guide](https://developer.apple.com/documentation/corehaptics/preparing-your-app-to-play-haptics)
lists iPad among devices without built-in haptic support. The backend queries
`CHHapticEngine.capabilitiesForHardware.supportsHaptics` and reports unsupported
hardware explicitly. It never substitutes audio, controller vibration or a
different vibration API for a missing local actuator.

The in-game gear panel contains **Gyro**, **Device haptics**, and **Also with
controller**. The second switch enables local feedback; the third separately
opts the local actuator into controller play. By default a physical controller
suppresses local output, including when controller haptics were disabled.
Turning local feedback off retains the third switch's remembered choice.
Unsupported local rows are disabled. Controller/trigger and advanced gain
preferences remain stored and retain their existing behavior. Settings apply
on return to the game.

The stronger composed side reaches the mono actuator once, rather than summing
both handles. Local gain defaults to 0.25 and is capped at 0.4; final intensity
cannot exceed 0.22. Sharpness retains the composed texture within a softer
range. Engine patterns contain haptics only. Each hardware effect expires in
100 ms and is renewed after 60 ms only while valid output remains. Silence and
explicit native stops stop the player. The local sink also requires an actual
game-presentation heartbeat newer than activation and less than 200 ms old.
This timestamp comes from the existing exact-build observer, including title
frames without a player; a live output timer alone cannot keep effects alive.

Backgrounding, pause, output disable, controller takeover, stale heartbeat,
engine reset/error and lifecycle changes cancel or invalidate local output.
The existing exact dialogue pause allowance is preserved. Engine callbacks
must match the current generation, and failures wait two seconds before a new
eligible effect can retry. The touch-only observer is explicitly rearmed after
loader initialization, so it works without a physical controller.

`controller-feedback.json` includes a `deviceHaptics` object with capability,
mode, gain, eligibility, engine state, successful start/parameter calls,
failures and stale-heartbeat counters. Successful API calls are not proof of
physical feel. `tools/test_feedback_policy.py` exercises eligibility, malformed
input, mono mixing, caps, freshness, lifecycle gates and real native/landing
composition. Physical intensity and texture acceptance still require an
installed build on a supported iPhone.
