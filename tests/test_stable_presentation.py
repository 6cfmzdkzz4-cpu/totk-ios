"""Compile the stable-picture and frame-cap policy without a device or game."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StablePresentation(unittest.TestCase):
    def test_duo_fixtures_cutscene_fit_and_frame_cap(self):
        compiler = shutil.which('clang') or shutil.which('cc')
        if not compiler:
            self.skipTest('A C compiler is required')
        with tempfile.TemporaryDirectory(prefix='totk-stable-presentation-') as directory:
            target = Path(directory) / 'presentation'
            build = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-fsanitize=address,undefined',
                                    str(ROOT / 'tests/test_stable_presentation.c'),
                                    str(ROOT / 'games/totk/runtime/totk_frame_cap.c'),
                                    '-o', str(target), '-lm'], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            result = subprocess.run([str(target)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('stable presentation checks passed', result.stdout)

    def test_launch_and_settings_keep_the_cap_honest(self):
        main = (ROOT / 'games/totk/ios/Sources/main.m').read_text()
        settings = (ROOT / 'games/totk/ios/Sources/TOTKSettings.m').read_text()
        present = (ROOT / 'games/totk/third_party/d2r_gfx/gfx/nvn/hle_nvn.c').read_text()
        viewport = (ROOT / 'games/totk/third_party/d2r_gfx/gfx/nvn/nvn_mtl.m').read_text()
        self.assertIn('setenv("TOTK_STABLE_ASPECT","1",1);', main)
        self.assertIn('setenv("TOTK_UNCAPPED","0",1);', main)
        self.assertIn('totk_stable_scene_picture', main)
        self.assertIn('90 and 120 are unavailable', settings)
        self.assertIn('No 90 or 120 gameplay patch was added', settings)
        self.assertNotIn('Saved; paces at 60', settings)
        self.assertIn('static const int rates[]={30,60,90,120};', settings)
        self.assertIn('totk_frame_cap_unavailable', settings)
        self.assertIn('totk_frame_cap_pace', present)
        self.assertIn('movie_host_cutscene_active', viewport)
        self.assertIn('TOTK_CUTSCENE_ASPECT', viewport)


if __name__ == '__main__':
    unittest.main()
