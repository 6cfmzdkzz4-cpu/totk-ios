"""Sandbox checks for an opt-in RomFS mod directory, with no game image."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RomfsMods(unittest.TestCase):
    def test_directory_rules(self):
        compiler = shutil.which('clang') or shutil.which('cc')
        if not compiler:
            self.skipTest('A C compiler is required')
        with tempfile.TemporaryDirectory(prefix='totk-romfs-mods-') as directory:
            target = Path(directory) / 'mods'
            build = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / 'tests/test_romfs_mods.c'),
                                    str(ROOT / 'games/totk/runtime/romfs_mods.c'),
                                    '-o', str(target)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            result = subprocess.run([str(target)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('RomFS mod directory checks passed', result.stdout)

    def test_warm_schedule_puts_learned_pipelines_first(self):
        source = (ROOT / 'games/totk/third_party/d2r_gfx/gfx/nvn/nvn_mtl.m').read_text()
        start = source.index('static void warm_start')
        body = source[start:source.index('if (!g_warm_items)', start)]
        self.assertLess(body.index('pso_user_path()'), body.index('if ([fm fileExistsAtPath:seed])'))
        self.assertLess(body.index('pso_shared_path()'), body.index('if ([fm fileExistsAtPath:seed])'))
        self.assertIn('learned render pipelines scheduled before prepared libraries', source)
        self.assertIn('learned pipeline compiled on the submit thread', source)


if __name__ == '__main__':
    unittest.main()
