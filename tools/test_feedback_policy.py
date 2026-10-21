#!/usr/bin/env python3
"""Run game-data-free feedback policy tests; optionally exercise private generated tables."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generated-dir', type=Path, help='private verified build folder for observer/table tests')
    args = parser.parse_args()
    tests = ['test_dualsense_feedback', 'test_haptic_cues', 'test_skydive_feedback']
    if args.generated_dir:
        tests += ['test_gameplay_feedback', 'test_haptic_generated']
    with tempfile.TemporaryDirectory(prefix='totk-policy-tests-') as folder:
        for name in tests:
            command = ['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                       '-Wno-deprecated-declarations', '-I'+str(ROOT/'games/totk/runtime'),
                       '-I'+str(ROOT/'games/totk/third_party/d2r_gfx/gfx/nvn'),
                       '-I'+str(ROOT/'games/totk/third_party/d2r_gfx/gfx/nvn/include')]
            if args.generated_dir:
                command += ['-I'+str(args.generated_dir.resolve())]
            exe = Path(folder)/name
            command += [str(ROOT/'games/totk/tests'/f'{name}.c'), '-o', str(exe)]
            subprocess.run(command, check=True)
            subprocess.run([str(exe)], check=True)
    print(f'{len(tests)} feedback policy test programs passed')


if __name__ == '__main__':
    main()
