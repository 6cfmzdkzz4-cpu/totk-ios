#!/usr/bin/env python3
"""Read-only check for the supplied base-1.0.0 Free Autobuild cheat.

The cheat text is user-supplied. Its license is undocumented, so this tool does
not install it, does not write main, and does not publish the cheat as a mod.
040E0000 is the Atmosphere/EdiZon header for a 4-byte write to MAIN. The next
field is the NSO memory offset. The 0x100 NSO header is not added. Base 1.0.0
code keeps those offsets; this runtime does not relocate main.

  python3 games/totk/scripts/check_free_autobuild_sites.py \
      --main extracted/v0/exefs/main \
      --reference /path/to/cheats/082CE09B06E33A12.txt

Exit 0 only when the pinned main SHA-256, the full 20-byte build id, the
reference-file SHA-256, and all three text sites agree. The JSON has classes
and one hash of the twelve original bytes. It does not contain instruction
words. Cost semantics stay unresolved.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import diag_autobuild_static as diag

REFERENCE_SHA256 = 'c245779f2f99396fc9727e5672dde8daaa9ff54f9b4a1902cedacb95aa41f0e1'
FULL_BUILD_ID = '082ce09b06e33a123cb1e2770f5f9147709033db'
BUILD_ID_PREFIX = '082ce09b06e33a12'
CHEAT_OPCODE = 0x040E0000
# opcode, memory offset, replacement word, replacement class
SITES = (
    (0x01714820, 0xD503201F, 'nop'),
    (0x01717610, 0x2A1F03E8, 'mov_w8_wzr'),
    (0x00D46270, 0xD503201F, 'nop'),
)
# Camera and aspect words written for the 1.0.0 iOS export, in addition to game_version.h.
EXTRA_RUNTIME_SITES = (0x377AC54, 0x1968C2C, 0x1E5EAE8)


def runtime_sites() -> set[int]:
    """1.0.0 offsets this runtime already patches. v0 addresses are identity."""
    header = Path(__file__).resolve().parents[1] / 'runtime' / 'game_version.h'
    lines = header.read_text().splitlines()
    else_at = max(index for index, line in enumerate(lines) if line.startswith('#else'))
    sites = set(EXTRA_RUNTIME_SITES)
    for line in lines[else_at:]:
        if line.startswith('#endif'):
            break
        if line.startswith('#define GAME_ADDR_0x'):
            sites.add(int(line.split()[2], 16))
    return sites


def parse_reference(text: str) -> list[tuple[int, int]]:
    found = []
    allowed = {(offset, word) for offset, word, _name in SITES}
    for raw in text.splitlines():
        line = raw.split('//', 1)[0].strip()
        if not line or line.startswith('['):
            continue
        parts = line.split()
        if len(parts) != 3:
            raise ValueError('a cheat line is not a 4-byte MAIN write')
        try:
            opcode, offset, word = (int(part, 16) for part in parts)
        except ValueError as exc:
            raise ValueError('a cheat line is not hexadecimal') from exc
        if opcode != CHEAT_OPCODE or (offset, word) not in allowed:
            raise ValueError('the cheat file is not the three supplied MAIN writes')
        found.append((offset, word))
    if len(found) != 3 or set(found) != allowed:
        raise ValueError('the cheat file does not contain exactly the three supplied writes')
    return found


def replacement_class(word: int) -> str:
    row = diag.detail_word(word)
    if word == 0xD503201F and row['class'] == 'nop':
        return 'nop'
    if row['class'] == 'mov_reg' and row['dests'] == [8] and row['uses'] == [31]:
        return 'mov_w8_wzr'
    raise ValueError('a supplied replacement is not the expected nop or mov w8, wzr')


def assess(raw: bytes, expect_sha: str, sites=SITES) -> dict:
    report = {
        'schema': 'free-autobuild-sites-1',
        'apply': False,
        'semantic': 'unresolved',
        'cost_verified': False,
        'address_space': 'NSO memory offset; 040E0000 selects MAIN; the 0x100 header is not added',
        'sha256_match': hashlib.sha256(raw).hexdigest() == expect_sha,
    }
    if not report['sha256_match']:
        report.update(status='sha_mismatch', bytes_read=False)
        return report
    header = diag.parse_header(raw)
    image = diag.memory_image(raw, header)
    text = header['segments'][0]
    occupied = runtime_sites()
    report.update(
        status='classified',
        build_id=header['build_id'],
        build_id_match=header['build_id'] == FULL_BUILD_ID,
        build_id_prefix_match=header['build_id'].startswith(BUILD_ID_PREFIX),
        segment_hashes_verified=header['segment_hashes_verified'],
        bytes_read=header['build_id'] == FULL_BUILD_ID,
    )
    if not report['bytes_read']:
        report['sites'] = []
        return report
    rows = []
    originals = bytearray()
    for offset, _word, expect_name in sites:
        if replacement_class(_word) != expect_name:
            raise ValueError('replacement class drifted')
        mapping = diag.resolve_address(header, offset)
        item = {
            'memory_offset': hex(offset),
            'segment': mapping['segment'],
            'inside_text': mapping['segment'] == 'text' and offset % 4 == 0,
            'replacement_class': expect_name,
            'overlaps_runtime_patch': offset in occupied,
        }
        if item['inside_text']:
            word = struct.unpack_from('<I', image, offset)[0]
            originals += struct.pack('<I', word)
            detail = diag.detail_word(word)
            item['original_class'] = detail['class']
        else:
            item['original_class'] = None
        rows.append(item)
    report['sites'] = rows
    report['all_inside_text'] = all(item['inside_text'] for item in rows)
    report['overlaps_runtime_patch'] = any(item['overlaps_runtime_patch'] for item in rows)
    if len(originals) == 12:
        report['original_words_sha256'] = hashlib.sha256(originals).hexdigest()
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--main', type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--fixture', action='store_true')
    parser.add_argument('--expect-sha')
    args = parser.parse_args(argv)
    pinned, _romfs = diag.expected_hashes()
    if args.fixture:
        if not args.expect_sha:
            parser.error('--fixture requires --expect-sha')
        pinned = args.expect_sha
    elif args.expect_sha:
        parser.error('hash overrides require --fixture')
    if args.main is None:
        json.dump({'schema': 'free-autobuild-sites-1', 'status': 'private_image_not_provided', 'apply': False}, sys.stdout, indent=2)
        sys.stdout.write('\n')
        return 2
    try:
        report = assess(diag._read_regular(args.main), pinned)
        if args.reference is None:
            report['reference_status'] = 'not_provided'
        else:
            blob = diag._read_regular(args.reference)
            digest = hashlib.sha256(blob).hexdigest()
            report['reference_sha256_match'] = digest == REFERENCE_SHA256
            if not report['reference_sha256_match']:
                report['reference_status'] = 'sha_mismatch'
            else:
                parse_reference(blob.decode('utf-8'))
                report['reference_status'] = 'exact_three_writes'
    except (OSError, ValueError, UnicodeError, json.JSONDecodeError) as exc:
        json.dump({'schema': 'free-autobuild-sites-1', 'status': 'refused', 'reason': str(exc), 'apply': False}, sys.stdout, indent=2)
        sys.stdout.write('\n')
        return 2
    ready = (
        report.get('sha256_match') and report.get('build_id_match') and report.get('all_inside_text')
        and not report.get('overlaps_runtime_patch') and report.get('reference_status') == 'exact_three_writes'
    )
    json.dump(report, sys.stdout, indent=2)
    sys.stdout.write('\n')
    return 0 if ready else 2


if __name__ == '__main__':
    sys.exit(main())
