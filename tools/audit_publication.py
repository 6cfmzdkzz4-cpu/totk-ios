#!/usr/bin/env python3
"""Reject game payloads, personal signing data and credentials in the publication tree."""
from __future__ import annotations

import argparse
from pathlib import Path, PurePosixPath
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN_SUFFIXES = {'.nsp', '.xci', '.nsz', '.xcz', '.nca', '.nso', '.npdm', '.nro',
    '.keys', '.key', '.pem', '.p12', '.pfx', '.cer', '.mobileprovision', '.provisionprofile',
    '.sav', '.ipa', '.metallib', '.air', '.spv', '.bwav', '.bars', '.bfttf', '.ttf',
    '.png', '.jpg', '.jpeg', '.icns', '.car', '.log', '.dylib', '.a', '.bin'}
FORBIDDEN_PARTS = {'node_modules', '.venv', 'extracted', 'rewritten', 'user-data',
    'backups', 'DerivedData', '_CodeSignature', 'GameData', 'Gen', 'inputs', 'outputs'}
GENERATED_HEADERS = {'gameplay_audio_fingerprints.h', 'gameplay_extra_audio.h',
    'gameplay_haptic_patterns.h', 'gameplay_dialogue_flows.h', 'runtime_tables.h',
    'romfs_config.h', 'shared_fonts_config.h', 'totk_live_layouts.h',
    'totk-live-display-metadata.json', 'embed.S'}
PATTERNS = [
    ('GitHub credential', re.compile(rb'gh[opsu]_[A-Za-z0-9]{24,}')),
    ('AWS access identifier', re.compile(rb'AKIA[A-Z0-9]{16}')),
    ('private key', re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----')),
    ('personal absolute path', re.compile(rb'/Users/[A-Za-z0-9_.-]+/')),
    ('physical device identifier', re.compile(rb'00008[0-9]{3}-[0-9A-Fa-f]{16}')),
]


def violations(name: str, data: bytes, symlink=False) -> list[str]:
    """Check actual candidate bytes; never print credential values on failure."""
    path = PurePosixPath(name)
    found = []
    if symlink:
        found.append('symlink needs explicit publication review')
    if path.suffix.lower() in FORBIDDEN_SUFFIXES or any(p.endswith('.app') for p in path.parts):
        found.append('forbidden payload/signing/output extension')
    if any(p in FORBIDDEN_PARTS for p in path.parts) or path.name in GENERATED_HEADERS:
        found.append('private or generated build path')
    if b'\0' in data:
        found.append('binary payload in source-only repository')
    for label, pattern in PATTERNS:
        if pattern.search(data):
            found.append(label)
    return found


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])


def audit_current():
    paths = set(git('ls-files', '-z', '--cached', '--others', '--exclude-standard').split(b'\0'))
    errors = []
    for raw in sorted(paths - {b''}):
        name = raw.decode()
        path = ROOT / name
        if not path.exists() and not path.is_symlink():
            continue
        data = str(path.readlink()).encode() if path.is_symlink() else path.read_bytes()
        errors += [(name, issue) for issue in violations(name, data, path.is_symlink())]
    return len(paths - {b''}), errors


def audit_history():
    """Audit every reachable commit, including files later removed from HEAD."""
    errors, seen, count = [], set(), 0
    for revision in git('rev-list', '--all').decode().splitlines():
        for entry in git('ls-tree', '-r', '-z', revision).split(b'\0'):
            if not entry:
                continue
            info, name = entry.split(b'\t', 1)
            mode, kind, oid = info.split()
            if kind != b'blob':
                errors.append((name.decode(), 'non-blob repository member'))
                continue
            key = (name, oid)
            if key in seen:
                continue
            seen.add(key)
            count += 1
            data = git('cat-file', 'blob', oid.decode())
            errors += [(name.decode(), issue) for issue in violations(name.decode(), data, mode == b'120000')]
    return count, errors


def audit_staged():
    """Inspect the index blobs themselves, including partially staged files."""
    errors, count = [], 0
    for entry in git('ls-files', '--stage', '-z').split(b'\0'):
        if not entry:
            continue
        info, raw_name = entry.split(b'\t', 1)
        mode, oid, stage = info.split()
        name = raw_name.decode()
        count += 1
        if stage != b'0' or mode not in (b'100644', b'100755', b'120000'):
            errors.append((name, 'unmerged or non-blob index member'))
            continue
        data = git('cat-file', 'blob', oid.decode())
        errors += [(name, issue) for issue in violations(name, data, mode == b'120000')]
    return count, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--history', action='store_true', help='also scan all reachable Git commits before pushing')
    parser.add_argument('--staged', action='store_true', help='also scan the exact staged index blobs')
    args = parser.parse_args()
    count, errors = audit_current()
    if args.staged:
        staged_count, staged_errors = audit_staged()
        count += staged_count
        errors += staged_errors
    if args.history:
        history_count, history_errors = audit_history()
        count += history_count
        errors += history_errors
    for name, issue in sorted(set(errors)):
        print(f'REJECT {name}: {issue}')
    if errors:
        raise SystemExit(1)
    print(f'Publication audit passed: {count} source candidates/blob versions checked')


if __name__ == '__main__':
    main()
