#!/usr/bin/env python3
"""Reproduce all local feedback headers from a verified base-1.0.0 RomFS."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

import prepare_controller_audio
import prepare_dialogue_feedback
import prepare_haptic_cues
from feedback_common import (EVFL_COMMIT, EVFL_REPOSITORY, EVFL_SOURCE_TREE_SHA256,
                             FeedbackInputError, REWRITTEN_MAIN_SHA256, ROMFS_SHA256,
                             VerifiedRomFS, evfl_tree_digest)


EXPECTED_HEADER_SHA256 = {
    "gameplay_audio_fingerprints.h": "e5d6760e995394e3ae2df8b0ecfbea6f47ab802a3c4ab8b2ba65f26cff866c74",
    "gameplay_extra_audio.h": "9b956e99b5c90dc2ddf4291e1830aaf849601ebc911700a537aa4e714102e60e",
    "gameplay_haptic_patterns.h": "e313568c27e59d23c5bec8f572a5fea7c057de3867551932bc76f39b962e159f",
    "gameplay_dialogue_flows.h": "d4b32d6c5ef83241c3a539769660099cbed054b1eb7490bf87a6afb9f11cd464",
}

REPORT_NAMES = {
    "controller": "controller-audio.json",
    "haptics": "haptic-cues.json",
    "dialogue": "dialogue-feedback.json",
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def atomic_copy(source: Path, destination: Path):
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + f".tmp-{os.getpid()}")
    try:
        temporary.write_bytes(source.read_bytes())
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def _evfl_checkout_valid(path: Path) -> bool:
    try:
        if evfl_tree_digest(path / "evfl") != EVFL_SOURCE_TREE_SHA256:
            return False
        head = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"], check=True,
                              capture_output=True, text=True).stdout.strip()
        return head == EVFL_COMMIT
    except (OSError, subprocess.CalledProcessError, FeedbackInputError):
        return False


def ensure_evfl(tooling_dir: Path | str) -> Path:
    """Fetch the exact parser commit into ignored build tooling; never install it globally."""
    tooling_dir = Path(tooling_dir)
    target = tooling_dir / f"evfl-{EVFL_COMMIT[:12]}"
    if _evfl_checkout_valid(target):
        return target
    tooling_dir.mkdir(parents=True, exist_ok=True)
    if target.exists():
        shutil.rmtree(target)
    temporary = tooling_dir / f".{target.name}.tmp-{os.getpid()}"
    shutil.rmtree(temporary, ignore_errors=True)
    try:
        subprocess.run(["git", "init", "-q", str(temporary)], check=True, capture_output=True, text=True)
        subprocess.run(["git", "-C", str(temporary), "remote", "add", "origin",
                        EVFL_REPOSITORY + ".git"], check=True, capture_output=True, text=True)
        subprocess.run(["git", "-C", str(temporary), "fetch", "-q", "--depth=1", "origin", EVFL_COMMIT],
                       check=True, capture_output=True, text=True)
        subprocess.run(["git", "-C", str(temporary), "checkout", "-q", "--detach", "FETCH_HEAD"],
                       check=True, capture_output=True, text=True)
        if not _evfl_checkout_valid(temporary):
            raise FeedbackInputError("fetched EVFL source did not match its pinned commit and tree hash")
        try:
            os.replace(temporary, target)
        except OSError:
            if not _evfl_checkout_valid(target):
                raise
        return target
    except subprocess.CalledProcessError as exc:
        detail = (exc.stderr or "").strip().splitlines()
        message = detail[-1] if detail else "git fetch failed"
        raise FeedbackInputError(f"could not fetch pinned EVFL parser: {message}") from exc
    finally:
        shutil.rmtree(temporary, ignore_errors=True)


def generate_all(romfs: Path | str, output_dir: Path | str, report_dir: Path | str,
                 evfl_source: Path | str | None = None, tooling_dir: Path | str | None = None) -> dict:
    """Generate and hash-lock all four headers before publishing any destination file."""
    romfs, output_dir, report_dir = Path(romfs), Path(output_dir), Path(report_dir)
    source = VerifiedRomFS(romfs)  # fail before starting expensive scans
    source.verify_rewritten_main()
    if evfl_source is None:
        if tooling_dir is None:
            try:
                tooling_dir = romfs.resolve().parents[2] / "tooling" / "feedback"
            except IndexError as exc:
                raise FeedbackInputError("cannot derive private tooling directory from RomFS path") from exc
        evfl_source = ensure_evfl(tooling_dir)
    tools = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="totk-feedback-") as folder:
        stage = Path(folder)
        headers, reports = stage / "headers", stage / "reports"
        controller_report = prepare_controller_audio.generate(
            romfs, headers / "gameplay_audio_fingerprints.h", reports / REPORT_NAMES["controller"], emit=False
        )
        haptic_report = prepare_haptic_cues.generate(
            romfs, headers, reports / REPORT_NAMES["haptics"], emit=False
        )
        dialogue_report = prepare_dialogue_feedback.generate(
            romfs, headers / "gameplay_dialogue_flows.h", reports / REPORT_NAMES["dialogue"],
            Path(evfl_source), emit=False,
        )
        actual = {name: digest(headers / name) for name in EXPECTED_HEADER_SHA256}
        if actual != EXPECTED_HEADER_SHA256:
            changed = {name: {"expected": EXPECTED_HEADER_SHA256[name], "actual": actual.get(name)}
                       for name in EXPECTED_HEADER_SHA256 if actual.get(name) != EXPECTED_HEADER_SHA256[name]}
            raise ValueError(f"feedback header reproduction mismatch: {json.dumps(changed, sort_keys=True)}")
        for name in EXPECTED_HEADER_SHA256:
            atomic_copy(headers / name, output_dir / name)
        for name in REPORT_NAMES.values():
            atomic_copy(reports / name, report_dir / name)
        source_files = {}
        for name in ("feedback_common.py", "prepare_controller_audio.py", "prepare_haptic_cues.py",
                     "prepare_dialogue_feedback.py", "prepare_feedback.py", "requirements-feedback.txt"):
            path = tools / name
            source_files[name] = digest(path)
        manifest = {
            "schema": "totk-feedback-generation-v1",
            "source_romfs_sha256": source.index["romfs_sha256"],
            "supported_source_romfs_sha256": ROMFS_SHA256,
            "rewritten_main_sha256": REWRITTEN_MAIN_SHA256,
            "evfl": {"repository": EVFL_REPOSITORY, "commit": EVFL_COMMIT,
                     "source_tree_sha256": dialogue_report["parser_source_tree_sha256"]},
            "headers": actual,
            "reports": {name: digest(report_dir / name) for name in REPORT_NAMES.values()},
            "generator_sources": source_files,
            "counts": {
                "guard_fingerprints": controller_report["unambiguous_guard_fingerprints"],
                "melee_fingerprints": controller_report["unambiguous_player_melee_fingerprints"],
                "extra_haptic_cues": len(haptic_report["cues"]),
                "dialogue_event_ids": dialogue_report["runtime_npc_event_ids"],
            },
        }
        manifest_path = stage / "feedback-generation.json"
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        atomic_copy(manifest_path, report_dir / manifest_path.name)
    print(json.dumps({"headers": actual, "counts": manifest["counts"]}, sort_keys=True))
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--romfs", required=True, type=Path,
                        help="verified base-1.0.0 RomFS directory containing index.json and romfs.bin")
    parser.add_argument("--output-dir", required=True, type=Path,
                        help="ignored build directory that will receive the four generated headers")
    parser.add_argument("--report-dir", required=True, type=Path,
                        help="ignored work directory for provenance reports (reports contain game-derived names)")
    parser.add_argument("--evfl-source", type=Path,
                        help="optional local checkout/package directory for the exact pinned EVFL source")
    parser.add_argument("--tooling-dir", type=Path,
                        help="private parser cache (default: the RomFS work tree's tooling/feedback directory)")
    args = parser.parse_args()
    generate_all(args.romfs, args.output_dir, args.report_dir, args.evfl_source, args.tooling_dir)


if __name__ == "__main__":
    main()
