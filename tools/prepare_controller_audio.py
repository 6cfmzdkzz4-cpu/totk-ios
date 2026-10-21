#!/usr/bin/env python3
"""Prepare unique local guard and player melee cue fingerprints from verified 1.0.0 RomFS."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import struct

from feedback_common import VerifiedRomFS, bars_members, channel_info


def generate(romfs: Path, header: Path, report_path: Path, emit: bool = True):
    source = VerifiedRomFS(romfs)
    index = source.index
    image = source.image
    fingerprints = {}
    scanned = 0
    empty = 0
    short_channels = 0
    header_only_channels = 0
    with image.open('rb') as stream:
        for row in index['files']:
            if not row['path'].endswith('.bwav'):
                continue
            if not row['size']:
                empty += 1
                continue
            stream.seek(row['offset'])
            bwav_header = stream.read(min(row['size'], 0x400))
            assert bwav_header[:8] == b'BWAV\xff\xfe\x01\x00', row['path']
            channels = channel_info(bwav_header)
            scanned += 1
            for info in channels:
                if info['codec'] != 1:
                    continue
                # BWAV's +0x34 is the stored data start; +0x30 describes the full stream for prefetch files.
                offset = info['offset']
                assert offset > 0, row['path']
                if offset >= row['size']:
                    header_only_channels += 1
                    continue
                if offset > row['size']-128:
                    short_channels += 1
                    continue
                stream.seek(row['offset']+offset)
                prefix = stream.read(128)
                assert len(prefix) == 128
                fingerprints.setdefault(prefix, []).append((row['path'], info['channel'], info['rate']))
    selected = []
    rejected = []
    for prefix, sources in fingerprints.items():
        guards = [s for s in sources if Path(s[0]).stem.startswith('Guard_')]
        if not guards:
            continue
        if len(guards) != len(sources) or any(rate != 48000 for _, _, rate in sources):
            rejected.extend(path for path, _, _ in guards)
            continue
        cue = Path(guards[0][0]).stem
        selected.append((cue, prefix, sources))
    selected.sort()
    assert len(selected) >= 20, 'Too few unambiguous guard cues'
    def melee_kind(path):
        name=Path(path).stem
        if not re.fullmatch(r'NSword_Whoosh(?:Finish)?_Player_(?:S|M|L|XL)_(?:Blunt_Wood|Sharp_Metal|Sharp_Wood)_\d+',name):
            return 0
        return 1 if '_Blunt_' in name else 2
    melee=[]
    melee_rejected=[]
    for prefix,sources in fingerprints.items():
        kinds=[melee_kind(path) for path,_,_ in sources]
        if not any(kinds):continue
        if not all(kinds) or len(set(kinds))!=1 or any(rate!=48000 for _,_,rate in sources):
            melee_rejected.extend(path for path,_,_ in sources if melee_kind(path));continue
        melee.append((Path(sources[0][0]).stem,prefix,kinds[0],sources))
    melee.sort()
    assert len(melee)>=40,'Too few unambiguous player melee cues'
    # The game normally plays prefetched BARS copies. Verify every selected cue's actual paired payload too.
    bars = source.read('Sound/Resource/M_Col_Static.bars.zs')
    count, = struct.unpack_from('<I', bars, 12)
    target_by_name = {cue: prefix for cue, prefix, _ in selected}
    target_prefixes = set(target_by_name.values())
    verified = set()
    for name, _, channels in bars_members(bars):
        for info in channels:
            if info['codec'] != 1:
                continue
            prefix = bars[info['offset']:info['offset']+128]
            if name in target_by_name:
                assert prefix == target_by_name[name], name
                verified.add(name)
            elif not name.startswith('Guard_'):
                assert prefix not in target_prefixes, f'Ambiguous BARS cue: {name}'
    assert verified == set(target_by_name)
    melee_bars=source.read('Sound/Resource/Weapon_Direct.bars.zs')
    melee_count,=struct.unpack_from('<I',melee_bars,12)
    melee_by_name={Path(path).stem:prefix for _,prefix,_,sources in melee for path,_,_ in sources}
    melee_prefixes=set(melee_by_name.values())
    melee_verified=set()
    for name,_,channels in bars_members(melee_bars):
        for info in channels:
            if info['codec']!=1:continue
            prefix=melee_bars[info['offset']:info['offset']+128]
            if name in melee_by_name:
                assert prefix==melee_by_name[name],name
                melee_verified.add(name)
            else:assert prefix not in melee_prefixes,f'Ambiguous player BARS cue: {name}'
    assert melee_verified==set(melee_by_name)
    lines = ['/* Generated from the verified local game; do not redistribute game-derived prefixes. */',
             'static const struct { const char *name; unsigned char prefix[128]; } guard_audio_fingerprints[] = {']
    for cue, prefix, _ in selected:
        values = ','.join(f'0x{x:02x}' for x in prefix)
        lines.append(f'    {{"{cue}",{{{values}}}}},')
    lines.extend(['};', '#define GUARD_AUDIO_FINGERPRINT_COUNT (sizeof guard_audio_fingerprints / sizeof guard_audio_fingerprints[0])', ''])
    lines.append('static const struct { const char *name; unsigned char prefix[128]; int kind; } melee_audio_fingerprints[] = {')
    for cue,prefix,kind,_ in melee:
        values=','.join(f'0x{x:02x}' for x in prefix)
        lines.append(f'    {{"{cue}",{{{values}}},{kind}}},')
    lines.extend(['};','#define MELEE_AUDIO_FINGERPRINT_COUNT (sizeof melee_audio_fingerprints / sizeof melee_audio_fingerprints[0])',''])
    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text('\n'.join(lines))
    report = {'source_romfs_sha256': index['romfs_sha256'], 'standalone_bwav_files_checked': scanned,
              'empty_resource_placeholders_skipped': empty, 'adpcm_channels_shorter_than_128_bytes': short_channels,
              'header_only_channel_placeholders_skipped': header_only_channels,
              'bars_entries_cross_checked': count, 'bars_guard_payloads_verified': len(verified),
              'bars_sha256': hashlib.sha256(bars).hexdigest(),
              'adpcm_channel_prefixes_checked': sum(map(len, fingerprints.values())),
              'fingerprint_bytes': 128, 'unambiguous_guard_fingerprints': len(selected),
              'cross_class_collision_rejections': rejected,
              'unambiguous_player_melee_fingerprints': len(melee),
              'melee_cross_class_collision_rejections': melee_rejected,
              'melee_bars_entries_cross_checked': melee_count,
              'melee_bars_payloads_verified': len(melee_verified),
              'melee_bars_sha256': hashlib.sha256(melee_bars).hexdigest(),
              'melee_cues': [{'name':cue,'kind':kind,'prefix_sha256':hashlib.sha256(prefix).hexdigest(),'sources':sources} for cue,prefix,kind,sources in melee],
              'header_sha256': hashlib.sha256(header.read_bytes()).hexdigest(),
              'cues': [{'name': cue, 'prefix_sha256': hashlib.sha256(prefix).hexdigest(),
                        'sources': sources} for cue, prefix, sources in selected],
              'scope': 'Exact encoded ADPCM cue identity; actual guarded player context is required for guard cues. Authored Player sharp/blunt swing cues lease shaping of actual native HD rumble. Audio carries no emitter pointer.'}
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2)+'\n')
    if emit:
        print(json.dumps({k: report[k] for k in ['standalone_bwav_files_checked', 'adpcm_channel_prefixes_checked', 'unambiguous_guard_fingerprints', 'cross_class_collision_rejections']}))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('romfs', type=Path)
    parser.add_argument('header', type=Path)
    parser.add_argument('report', type=Path)
    args = parser.parse_args()
    generate(args.romfs, args.header, args.report)


if __name__ == '__main__':
    main()
