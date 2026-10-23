#!/usr/bin/env python3
"""Prepare a personal base-1.0.0 TOTK iPad/iPhone app from verified local inputs."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))
import build_app as upstream

APP_FILENAME = f"{upstream.APP_NAME}.app"

# This exact permission-metadata variant is unused by the native runtime. It
# carries no valid Nintendo ACID signature. All executable and RomFS gates stay
# unchanged, and the metadata is never embedded in the personal app.
KNOWN_UNUSED_NPDM = "e78c40978c23b42866973e6fe506c2ad7716ec51c651f35bfeada3415bb3b634"


def accept_known_unused_metadata(tree: Path, game: Path) -> bool:
    """Recognize one exact metadata file only after binding all executable bytes."""
    folder = tree / "extracted/v0/exefs"
    metadata = folder / "main.npdm"
    if not metadata.is_file() or upstream.sha256(metadata) != KNOWN_UNUSED_NPDM:
        return False
    if not upstream.source_matches(folder.parent / "extraction-manifest.json", game):
        return False
    for name, digest in upstream.EXPECTED["exefs_sha256"].items():
        if name != "main.npdm":
            upstream.check(f"ExeFS {name}", upstream.sha256(folder / name), digest)
    data = metadata.read_bytes()
    if len(data) < 0x78 or data[:4] != b"META":
        return False
    aci = struct.unpack_from("<I", data, 0x70)[0]
    if aci > len(data) - 0x18:
        return False
    if struct.unpack_from("<Q", data, aci + 0x10)[0] != int(upstream.EXPECTED["title_id"], 16):
        return False
    upstream.EXPECTED["exefs_sha256"]["main.npdm"] = KNOWN_UNUSED_NPDM
    upstream.say("Accepting the exact known unused NPDM variant; its Nintendo signature is invalid. Executable/rewrite/RomFS checks remain mandatory.")
    return True


def parse_args(argv=None):
    """Expose the personal build, optional signing, and explicit install stages."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path, help="base 1.0.0 NSP or XCI")
    parser.add_argument("--keys", "-k", required=True, type=Path, help="your local prod.keys")
    fonts = parser.add_mutually_exclusive_group()
    fonts.add_argument("--firmware", type=Path, help="your unpacked firmware NCA directory")
    fonts.add_argument("--shared-fonts", type=Path, help="your previously extracted BFTTF font directory")
    parser.add_argument("--work", type=Path, default=REPO / "work", help="private build cache; never commit it")
    parser.add_argument("--out", type=Path, default=REPO / "dist" / APP_FILENAME, help="personal .app or .ipa output")
    parser.add_argument("--display", choices=("ipad", "duo", "iphone"), default="ipad",
                        help="ipad/duo: live narrow camera/HUD (landscape 1:1 to 16:9); iphone: static 239:110")
    parser.add_argument("--bundle-id", required=True, help="your stable application identifier; keep it for future updates")
    parser.add_argument("--cloud-container", default="", help="optional CloudKit container owned by your Apple team")
    parser.add_argument("--team", help="optional Apple development team ID for signing")
    parser.add_argument("--device", help="paired device UDID, required with --team")
    parser.add_argument("--install", action="store_true", help="install only after successful requested signing")
    parser.add_argument("--prepare-only", action="store_true", help="verify/extract inputs and regenerate display/feedback assets, without compiling")
    parser.add_argument("--build-number", type=int, default=27)
    parser.add_argument("--dotnet", default="dotnet", help=".NET 10 SDK executable used to build the source-only shader translator")
    return parser.parse_args(argv)


def validate_options(args):
    """Reject ambiguous identity, unsupported inputs, and unintended install modes."""
    if not re.fullmatch(r"[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+", args.bundle_id):
        raise upstream.BuildError("--bundle-id must be a reverse-domain application identifier")
    if args.cloud_container and not re.fullmatch(r"iCloud\.[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+", args.cloud_container):
        raise upstream.BuildError("--cloud-container must be an exact iCloud.<your identifier>")
    if bool(args.team) != bool(args.device):
        raise upstream.BuildError("provide both --team and --device to sign, or neither for an unsigned build")
    if args.install and not args.team:
        raise upstream.BuildError("--install requires --team and --device; unsigned apps cannot be installed")
    if args.prepare_only and (args.team or args.install):
        raise upstream.BuildError("--prepare-only cannot sign or install")
    if args.build_number < 1:
        raise upstream.BuildError("--build-number must be positive")
    for name in ("game", "keys", "work", "out", "firmware", "shared_fonts"):
        if value := getattr(args, name):
            setattr(args, name, value.expanduser().resolve())
    if not args.game.is_file() or not args.keys.is_file():
        raise upstream.BuildError("the supplied game file and prod.keys must exist")
    if upstream.input_kind(args.game) not in ("nsp", "xci"):
        raise upstream.BuildError("only an uncompressed base NSP or XCI is supported; updates and combined packages are not accepted here")
    if args.out.suffix.lower() not in (".app", ".ipa"):
        raise upstream.BuildError("--out must end in .app or .ipa")
    if args.firmware and not args.firmware.is_dir():
        raise upstream.BuildError("--firmware must be an unpacked local NCA directory")
    if args.shared_fonts:
        upstream.font_report(args.shared_fonts)
    if upstream.input_kind(args.game) == "nsp" and not (
        args.firmware or args.shared_fonts or upstream.fonts_valid(args.work / "totk/extracted/firmware/shared-fonts")
    ):
        raise upstream.BuildError("NSP needs --firmware or --shared-fonts; it does not contain system fonts")


def ensure_translator(tree, args, logs, env):
    """Build the redistributable host tool locally; generated shaders stay private."""
    dotnet = shutil.which(args.dotnet, path=env["PATH"])
    if not dotnet:
        raise upstream.BuildError("Install the .NET 10 SDK, or pass --dotnet /path/to/dotnet")
    version = subprocess.check_output([dotnet, "--version"], text=True, env=env).strip()
    if not version.startswith("10."):
        raise upstream.BuildError("the source-only translator build requires the .NET 10 SDK")
    sources = [REPO / "games/totk/tools/totk_xlate", REPO / "vendor/ryujinx"]
    records = [(p.relative_to(REPO).as_posix(), upstream.sha256(p))
               for folder in sources for p in sorted(folder.rglob("*"))
               if p.is_file() and not any(x in p.parts for x in ("obj", "bin", "__pycache__"))]
    fingerprint = hashlib.sha256(json.dumps([version, records]).encode()).hexdigest()
    output = tree / "prebuilt/macos-arm64"
    stamp = output / "source-build.json"
    translator = output / "totk_xlate"
    if stamp.is_file() and translator.is_file():
        cached = json.loads(stamp.read_text())
        if cached.get("source_fingerprint") == fingerprint and cached.get("executable_sha256") == upstream.sha256(translator):
            (output / "totk_xlate.sha256").write_text(cached["executable_sha256"] + "\n")
            return
    upstream.say("Building the shader translator from its licensed source (.NET NativeAOT)")
    build_env = dict(env, TOTK_XLATE_OUT=str(output), DOTNET_CLI_TELEMETRY_OPTOUT="1", DOTNET_NOLOGO="1")
    upstream.run(["zsh", REPO / "games/totk/tools/totk_xlate/publish_native.sh", dotnet], logs / "build-translator.log", cwd=REPO, env=build_env)
    stamp.write_text(json.dumps({"source_fingerprint": fingerprint, "sdk": version,
                                "executable_sha256": upstream.sha256(translator)}, indent=2) + "\n")


def main(argv=None):
    """Keep every game-derived output in the private work folder and personal app."""
    args = parse_args(argv)
    validate_options(args)
    py = upstream.prerequisites(args)
    args.work.mkdir(parents=True, exist_ok=True)
    logs = args.work / "logs"
    cached = (args.work / "totk/extracted/v0/romfs/romfs.bin").is_file()
    required = 25 if cached else (65 if args.out.suffix.lower() == ".ipa" else 45)
    if shutil.disk_usage(args.work).free < required * 2**30:
        raise upstream.BuildError(f"allow at least {required} GiB free on the work volume")
    env = dict(os.environ, PATH=os.pathsep.join(upstream.TOOL_PATHS + [os.environ.get("PATH", "")]))
    tree = upstream.prepare_work(args.work, py, logs)
    try:
        upstream.step_code(tree, args.game, args.keys, env, logs)
    except upstream.BuildError:
        if not accept_known_unused_metadata(tree, args.game):
            raise
        upstream.step_code(tree, args.game, args.keys, env, logs)
    upstream.step_data(tree, args.game, args.keys, logs, firmware=args.firmware, shared_fonts=args.shared_fonts)
    # Rehash the image, including reused caches: an index alone is not evidence
    # that current bytes match the verified base game.
    upstream.say("Checking complete local RomFS integrity")
    romfs = tree / "extracted/v0/romfs"
    upstream.check("RomFS", upstream.sha256(romfs / "romfs.bin"), upstream.EXPECTED["romfs"]["sha256"])
    def run(cmd, name):
        upstream.run(cmd, logs / name, cwd=tree, env=env)
    run([tree / ".venv/bin/python", REPO / "tools/prepare_feedback.py", "--romfs", romfs, "--output-dir", tree / "build", "--report-dir", tree / "analysis/feedback"], "prepare-feedback.log")
    narrow = args.display in ("ipad", "duo")
    maximum = "2752x2064" if narrow else "1920x1080"
    if narrow:
        run([tree / ".venv/bin/python", "scripts/make_output_res_overlay.py", maximum], "prepare-output.log")
    run([tree / ".venv/bin/python", REPO / "tools/prepare_display.py", tree, "--profile", "ipad" if narrow else "iphone"], "prepare-display.log")
    if narrow:
        metadata = tree / "build/totk-live-display-metadata.json"
        run([tree / ".venv/bin/python", REPO / "tools/prepare_live_display.py", tree, "--output", metadata], "prepare-live-display.log")
        run([tree / ".venv/bin/python", REPO / "tools/emit_live_display.py", metadata, tree / "build/totk_live_layouts.h"], "emit-live-display.log")
    receipt = {
        "game_version": "1.0.0", "title_id": upstream.EXPECTED["title_id"],
        "input_format": upstream.input_kind(args.game), "display": args.display,
        "bundle_id": args.bundle_id, "cloud_container": args.cloud_container or None,
        "romfs_sha256": upstream.EXPECTED["romfs"]["sha256"],
        "npdm_sha256": upstream.sha256(tree / "extracted/v0/exefs/main.npdm"),
        "known_unused_metadata_variant": upstream.sha256(tree / "extracted/v0/exefs/main.npdm") == KNOWN_UNUSED_NPDM,
        "rewritten_sha256": {name: upstream.sha256(tree / "rewritten/v0" / name) for name in upstream.EXPECTED["rewritten_sha256"]},
        "generated_feedback_sha256": {p.name: upstream.sha256(p) for p in sorted((tree / "build").glob("gameplay_*.h"))},
        "live_narrow_display": narrow,
        "live_layout_header_sha256": upstream.sha256(tree / "build/totk_live_layouts.h") if narrow else None,
        "prepared": True, "compiled": False, "signed": False, "installed": False,
    }
    receipt_path = args.work / "ios-build-receipt.json"
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
    if args.prepare_only:
        upstream.say(f"Verified inputs and generated private assets; receipt: {receipt_path}")
        return
    ensure_translator(tree, args, logs, env)
    upstream.step_shaders(tree, logs, version="v0")
    upstream.step_binary(tree, args.game, args.keys, env, logs, version="v0")
    personal_app = args.out if args.out.suffix.lower() == ".app" else args.work / "personal" / APP_FILENAME
    run([tree / ".venv/bin/python", "ios/tools/build_ipa.py", "--tree", tree, "--out", personal_app,
         "--version", "v0", "--logs", logs, "--bundle-id", args.bundle_id, "--display", args.display,
         "--build-number", str(args.build_number), "--cloud-container", args.cloud_container, "--app-only"], "build-ios.log")
    receipt["compiled"] = True
    receipt["ios_dependencies"] = json.loads((tree / "build/ios-deps/manifest.json").read_text())
    if args.team:
        from ios_sign import sign_app
        sign_app(personal_app, args.team, args.device, args.cloud_container, args.work / "signing")
        receipt["signed"] = True
    if args.out.suffix.lower() == ".ipa":
        payload = args.work / "ipa/Payload"
        upstream.say("Packing the personal IPA; this contains your game and must stay private")
        if payload.parent.exists():
            shutil.rmtree(payload.parent)
        payload.mkdir(parents=True)
        copied = subprocess.run(["cp", "-cR", str(personal_app), str(payload / APP_FILENAME)], capture_output=True)
        if copied.returncode:
            shutil.rmtree(payload / APP_FILENAME, ignore_errors=True)
            shutil.copytree(personal_app, payload / APP_FILENAME)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.unlink(missing_ok=True)
        upstream.run(["zip", "-qry", "-0", args.out, "Payload"], logs / "pack-ipa.log", cwd=payload.parent)
    if args.install:
        subprocess.run(["xcrun", "devicectl", "device", "install", "app", "--device", args.device, str(personal_app), "--timeout", "1800"], check=True)
        receipt["installed"] = True
    receipt["executable_sha256"] = upstream.sha256(personal_app / "TOTK")
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
    upstream.say(f"Personal app ready: {args.out}; receipt: {receipt_path}")


if __name__ == "__main__":
    try:
        main()
    except (upstream.BuildError, OSError, subprocess.CalledProcessError, RuntimeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
