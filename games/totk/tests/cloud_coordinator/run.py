#!/usr/bin/env python3
"""Run game-free coordinator durability/recovery checks with real Foundation I/O."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import uuid


def run(output, sanitize):
    """Compile only the small host fixture; isolate all files beneath output."""
    here = Path(__file__).resolve().parent
    game = here.parents[1]
    command = ["xcrun", "clang", "-fobjc-arc", "-fblocks", "-Wall", "-Wextra", "-Werror",
               "-Wno-deprecated-declarations", "-Wno-incompatible-pointer-types",
               "-I" + str(here / "shims"), str(here / "probe.m"),
               "-framework", "Foundation", "-framework", "CloudKit", "-framework", "CoreGraphics"]
    if sanitize:
        command += ["-fsanitize=" + sanitize, "-fno-omit-frame-pointer", "-g", "-O1"]
    executable = output / "cloud-coordinator-probe"
    command += ["-o", str(executable)]
    compiled = subprocess.run(command, capture_output=True, text=True, timeout=45)
    (output / "compile.log").write_text(compiled.stdout + compiled.stderr)
    if compiled.returncode:
        raise RuntimeError(compiled.stderr)
    cases = output / ("cases-" + uuid.uuid4().hex)
    result = subprocess.run([str(executable), str(cases)], capture_output=True, text=True, timeout=45)
    (output / "runtime.jsonl").write_text(result.stdout)
    (output / "runtime.stderr").write_text(result.stderr)
    report = json.loads((cases / "observations.json").read_text()) if (cases / "observations.json").is_file() else {}
    report.update(compile_command=command, compile_exit=0, runtime_exit=result.returncode,
                  source_sha256={name: hashlib.sha256((game / name).read_bytes()).hexdigest()
                                 for name in ("ios/Sources/TOTKCloudSave.m", "ios/Sources/TOTKSaveArchive.m")},
                  game_builds=False, device_operations=False)
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: report.get(k) for k in ("status", "checks", "failures", "runtime_exit")}))
    if result.returncode or report.get("status") != "passed":
        raise RuntimeError(result.stdout[-5000:] + result.stderr[-2000:])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, help="retain fixture logs in this private folder")
    parser.add_argument("--sanitize", choices=("undefined", "address,undefined"))
    args = parser.parse_args()
    if args.out:
        output = args.out.expanduser().resolve()
        output.mkdir(parents=True, exist_ok=True)
        run(output, args.sanitize)
    else:
        with tempfile.TemporaryDirectory(prefix="totk-cloud-checks-") as folder:
            run(Path(folder), args.sanitize)


if __name__ == "__main__":
    main()
