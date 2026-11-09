"""The iOS command file is configured after constructors. The reader must start then, once."""
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


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


if __name__ == '__main__':
    unittest.main()
