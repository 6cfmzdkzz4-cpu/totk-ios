"""The iOS command file is configured after constructors. The reader must start then, once."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT/'games/totk/runtime'
INCLUDE_C = re.compile(r'^\s*#include\s+"([^"]+\.(?:c|m|mm|S|s))"')


class InputFileStart(unittest.TestCase):
    def test_late_path_starts_once_and_each_flag_is_read_once(self):
        source = ROOT/'tests/test_input_file_start.c'
        binary = Path('/tmp/totk-input-file-start')
        build = subprocess.run(
            ['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread', str(source), '-o', str(binary)],
            capture_output=True, text=True)
        self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
        run = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn('input file start checks passed', run.stdout)

    def test_ios_starts_the_reader_after_setting_the_path(self):
        main = (ROOT/'games/totk/ios/Sources/main.m').read_text()
        launch = (ROOT/'games/totk/app/launcher.m').read_text()
        reader = (ROOT/'games/totk/runtime/input.c').read_text()
        set_at = main.index('setenv("TOTK_INPUT_FILE"')
        start_at = main.index('runtime_input_file_start();')
        self.assertLess(set_at, start_at)
        self.assertIn('execv(arguments[0], arguments);', launch)
        self.assertLess(launch.index('setenv("TOTK_INPUT_FILE"'), launch.index('execv(arguments[0], arguments);'))
        self.assertIn('input_file_ensure_started(getenv("TOTK_INPUT_FILE"), input_file, runtime_fail);', reader)
        self.assertNotIn('#include "input_file_start.c"', reader)

    def test_each_build_owns_the_helper_once_and_links_it(self):
        yml = (ROOT/'games/totk/ios/project.yml').read_text()
        block = yml.split('path: ../runtime', 1)[1].split('path: ../third_party', 1)[0]
        excludes = set(re.findall(r'- (\S+)', block.split('excludes:', 1)[1]))
        suffixes = {'.c', '.m', '.mm', '.S', '.s'}
        ios = [p.name for p in RUNTIME.iterdir() if p.suffix in suffixes and p.name not in excludes]
        ios.append('loader.c')
        self.assertEqual(ios.count('loader.c'), 1)
        self.assertIn('input_file_start.c', ios)
        self.assertIn('input.c', ios)
        owned = set(ios)
        for name in owned:
            for included in INCLUDE_C.findall((RUNTIME/name).read_text()):
                self.assertNotIn(Path(included).name, owned, name)
        for line in (ROOT/'games/totk/Makefile').read_text().splitlines():
            if 'runtime/input.c' in line:
                self.assertEqual(line.count('runtime/input_file_start.c'), 1, line[:80])
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            helper = RUNTIME/'input_file_start.c'
            obj = out/'helper.o'
            built = subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-c', str(helper), '-o', str(obj)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            driver = out/'driver.c'
            driver.write_text('#include "input_file_start.h"\nint main(void){return input_file_ensure_started(0,0,0);}\n')
            good = subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread', str(driver), str(obj),
                                   '-I', str(RUNTIME), '-o', str(out/'good')], capture_output=True, text=True)
            self.assertEqual(good.returncode, 0, good.stderr)
            ran = subprocess.run([str(out/'good')], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)
            dup = out/'dup.c'
            dup.write_text('#include "input_file_start.c"\nint main(void){return 0;}\n')
            bad = subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread', str(dup), str(obj),
                                  '-I', str(RUNTIME), '-o', str(out/'bad')], capture_output=True, text=True)
            self.assertNotEqual(bad.returncode, 0)
            self.assertIn('multiple definition', bad.stderr)


if __name__ == '__main__':
    unittest.main()
