"""A phone call must stop RemoteIO and start it again without stalling audren."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SESSION = (ROOT/'games/totk/ios/Sources/TOTKAudioSession.m').read_text()
MIXER = (ROOT/'games/totk/runtime/audio_mixer.c').read_text()
MAIN = (ROOT/'games/totk/ios/Sources/main.m').read_text()
INPUT = (ROOT/'games/totk/runtime/input.c').read_text()


class AudioInterruption(unittest.TestCase):
    def test_session_observes_calls_routes_and_foreground(self):
        self.assertIn('AVAudioSessionCategoryPlayback', SESSION)
        self.assertIn('AVAudioSessionCategoryOptionMixWithOthers', SESSION)
        self.assertIn('setActive:YES', SESSION)
        self.assertIn('AVAudioSessionInterruptionNotification', SESSION)
        self.assertIn('AVAudioSessionMediaServicesWereResetNotification', SESSION)
        self.assertIn('AVAudioSessionRouteChangeNotification', SESSION)
        self.assertIn('UIApplicationDidBecomeActiveNotification', SESSION)
        self.assertIn('UIApplicationWillEnterForegroundNotification', SESSION)
        self.assertIn('sceneWillEnterForeground:', MAIN)
        self.assertIn('totk_audio_session_start();', MAIN)
        self.assertIn('[audio] interruption began', SESSION)
        self.assertIn('[audio] interruption ended resume=%d ok=%d', SESSION)
        self.assertIn('[audio] restart ok', SESSION)
        self.assertIn('[audio] restart err', SESSION)
        self.assertIn('Games resume even when the system leaves shouldResume unset.', SESSION)
        handler = SESSION[SESSION.index('static void on_interruption'):SESSION.index('static void on_reset')]
        self.assertLess(handler.index('AVAudioSessionInterruptionTypeBegan'), handler.index('should_resume'))
        self.assertIn('resume_after(atomic_load(&g_resume_gen), should_resume, 0);', handler)
        self.assertNotIn('if (!should_resume) return', SESSION)
        self.assertNotIn('if (should_resume)', SESSION)

    def test_renderer_drops_instead_of_blocking_and_flag_restarts(self):
        push = MIXER.index('static void output_push')
        body = MIXER[push:MIXER.index('static Mixer *g_live', push)]
        self.assertIn('host_paused', body)
        self.assertIn('return;', body[body.index('host_paused'):])
        self.assertIn('device stalled: drop this frame', MIXER)
        self.assertIn('audio_host_restart_flag', INPUT)
        self.assertIn('audiorestart', MIXER)
        self.assertIn('diag.txt', MIXER)
        close = MIXER.index('void audio_mixer_close')
        self.assertIn('output_dispose_locked', MIXER[close:close + 500])
        self.assertNotIn('audio_mixer_close(r)', MIXER[MIXER.index('int audio_host_set_interrupted'):MIXER.index('int audio_host_restart_flag')])


if __name__ == '__main__':
    unittest.main()
