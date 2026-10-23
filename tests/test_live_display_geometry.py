"""Compile production geometry/epoch policy with orientation and invalid-size fixtures."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LiveDisplayGeometry(unittest.TestCase):
    def test_proportions_density_caps_invalid_sizes_and_temporal_epochs(self):
        compiler = shutil.which('clang') or shutil.which('cc')
        if not compiler:
            self.skipTest('A C compiler is required')
        with tempfile.TemporaryDirectory(prefix='totk-live-geometry-') as directory:
            target = Path(directory) / 'geometry'
            build = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-fsanitize=address,undefined',
                                    str(ROOT / 'tests/test_live_display_geometry.c'),
                                    '-o', str(target), '-lm'], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            result = subprocess.run([str(target)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('live display geometry checks passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
