#!/usr/bin/env python3
"""Extract local, collision-checked audio identities and sound-shaped haptic envelopes."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct

from feedback_common import ROMFS_SHA256 as ROMFS_SHA, VerifiedRomFS, bars_container, bars_members, channel_info


def pcm_channel(data, info):
    """The same saturating Nintendo DSP ADPCM equation as runtime/audio_mixer.c."""
    assert info['codec'] == 1 and 8000 <= info['rate'] <= 96000
    h1, h2 = info['history']
    coefficients = info['coefficients']
    start = info['offset']
    count = info['stored_samples']
    assert 0 < count <= info['rate']*10
    assert start+((count+13)//14)*8 <= len(data)
    result = []
    for i in range(count):
        frame, nibble = divmod(i, 14)
        ps = data[start+frame*8]
        scale, predictor = 1 << (ps & 15), (ps >> 4) & 7
        value = (data[start+frame*8+1+nibble//2] >> (0 if nibble & 1 else 4)) & 15
        if value >= 8:
            value -= 16
        value = (value*scale*2048+1024+coefficients[predictor*2]*h1+
                 coefficients[predictor*2+1]*h2) >> 11
        value = min(32767, max(-32768, value))
        h2, h1 = h1, value
        result.append(value/32768)
    return result


def envelope(data, info, peak, limit_ms):
    infos = info if isinstance(info, list) else [info]
    rate = infos[0]['rate']
    assert all(x['rate'] == rate and x['stored_samples'] == infos[0]['stored_samples'] for x in infos)
    channels = [pcm_channel(data, item)[:limit_ms*rate//1000] for item in infos]
    windows = []
    window_size = max(1, round(rate/50))
    for start in range(0, len(channels[0]), window_size):
        blocks = [samples[start:start+window_size] for samples in channels]
        rms = math.sqrt(sum(x*x for block in blocks for x in block)/(len(blocks)*len(blocks[0])))
        difference = math.sqrt(sum((block[i]-block[i-1])**2 for block in blocks for i in range(1,len(block)))/
                               (len(blocks)*max(1,len(blocks[0])-1)))
        windows.append((rms, min(1, difference/(rms+1e-9))))
    maximum = max(x[0] for x in windows)
    assert maximum > 0.00001
    points = []
    for i, (rms, edge) in enumerate(windows):
        level = rms/maximum
        intensity = peak*math.sqrt(level) if level >= 0.035 else 0
        points.append((i*20, intensity, 0.12+0.68*edge))
    # Always end in silence; a cropped cue gets a short release rather than a held actuator.
    points.append((max(points[-1][0]+20, math.ceil(len(channels[0])*1000/rate)), 0, points[-1][2]))
    assert len(points) <= 512 and points[-1][0] <= 10000
    return points


def classify(name):
    if name == 'Sys_GameStart_Renewal': return 'START', 3, 0.24, 7200
    if name in ('Sys_Talk_Next','Sys_Talk_End'): return 'DIALOGUE', 3, 0.075, 100
    if re.fullmatch(r'PL_Water_SwimDash_\d+', name): return 'WATER_DASH', 3, 0.095, 360
    if re.fullmatch(r'Mt_\w+_Pl_Climb(?:Weak|Strong|Hard)_Hand(?:L|R|Accent)\d+',name):
        side = 1 if '_HandL' in name else 2 if '_HandR' in name else 3
        return 'CLIMB', side, 0.11 if 'Weak_' in name else 0.15, 180
    if name == 'NSword_Charge_Lv1': return 'CHARGE', 3, 0.18, 280
    # Player AttackSwing selects these NSword identities even for two-handed weapons.
    if re.fullmatch(r'NSword_Whoosh(?:Finish)?_Player_(?:S|M|L|XL)_(?:Blunt_Wood|Sharp_Metal|Sharp_Wood)_\d+',name):
        return 'SWING', 3, 0.08, 160 if '_Blunt_' in name else 120
    # Spear's Player AttackSwing branch uses these seven bounded size/material cues.
    if re.fullmatch(r'Spear_Whoosh_(?:Sharp|Blunt)_(?:S|M|L)_\d+',name):
        return 'THRUST', 3, 0.075, 120
    # SLink ArrangeAssetName expands Mt_Pl placeholders by the actual surface material.
    # These shared jump/naked-contact sounds require exact DashUp state at admission.
    if re.fullmatch(r'Mt_\w+_Pl_(?:Jump_\d+|MoveNaked_(?:R|Accent)00|ClimbWeak_Foot(?:L|Accent)00)',name):
        return 'CLIMB_DASH', 3, 0.13, 160
    return None


def fine_water_texture(points):
    """Keep the splash envelope, with a softer attack and a fine, bright wash."""
    duration = points[-1][0]
    output = []
    for time in list(range(0, duration, 8))+[duration]:
        i = next(i for i in range(1, len(points)) if points[i][0] >= time)
        left, right = points[i-1], points[i]
        phase = (time-left[0])/(right[0]-left[0])
        intensity = left[1]+phase*(right[1]-left[1])
        ramp = min(1.0, time/32.0, (duration-time)/80.0)
        ramp = ramp*ramp*(3-2*ramp)
        edge = left[2]+phase*(right[2]-left[2])
        output.append((time, intensity*ramp,
                       0.74+0.08*edge+0.018*math.sin(2*math.pi*time/24)))
    return output


def attack_texture(points, category, name):
    """Light dry swing/thrust accents leave the existing native impact policy intact."""
    sharp = category == 'THRUST' or '_Sharp_' in name
    duration = points[-1][0]
    result = []
    for time, intensity, edge in points:
        attack = min(1.0, time/12.0)
        release = min(1.0, (duration-time)/32.0)
        result.append((time, intensity*attack*release,
                       (0.72+0.16*edge) if sharp else (0.20+0.24*edge)))
    return result


def generate(romfs: Path, output_dir: Path, report_path: Path, emit: bool = True):
    source = VerifiedRomFS(romfs)
    index = source.index
    read = source.read
    # Every stored ADPCM channel contributes to the collision map, including non-Sound banks.
    sources, candidates = {}, {}
    with source.image.open('rb') as stream:
        for row in index['files']:
            if not row['path'].endswith('.bwav') or not row['size']:
                continue
            stream.seek(row['offset'])
            header = stream.read(min(row['size'], 0x400))
            name = Path(row['path']).stem
            for info in channel_info(header):
                off = info['offset']
                if info['codec'] != 1 or off+128 > row['size']:
                    continue
                stream.seek(row['offset']+off)
                prefix = stream.read(128)
                sources.setdefault(prefix, []).append((name, info['channel'], info['rate'], row['path']))
            if classify(name):
                candidates[name] = row
    verified = {}
    bars_proof = []
    # Prefetched banks also contain sounds absent from standalone streams.
    # Account for every bank before admitting any shared identity.
    bank_rows = [r for r in index['files'] if r['path'].endswith('.bars.zs') and r['size']]
    for row in bank_rows:
        data = bars_container(read(row['path']))
        names = []
        for name, base, channels in bars_members(data):
            for info in channels:
                if info['codec'] != 1 or info['stored_samples'] < 224:
                    continue
                assert base < info['offset'] and info['offset']+128 <= len(data)
                prefix = data[info['offset']:info['offset']+128]
                sources.setdefault(prefix, []).append((name,info['channel'],info['rate'],row['path']+'#'+name))
            if not classify(name):
                continue
            names.append(name)
            verified.setdefault(name, []).append(row['path'])
            candidates.setdefault(name, dict(bars=row['path'],base=base))
        bars_proof.append(dict(path=row['path'],sha256=hashlib.sha256(data).hexdigest(),matched_names=names))
    chosen, rejected = [], []
    for name in sorted(candidates):
        if name not in verified:
            rejected.append(dict(name=name, reason='No verified paired BARS payload'))
            continue
        row = candidates[name]
        if 'bars' in row:
            data = bars_container(read(row['bars']))
            channels = channel_info(data,row['base'])
            source_path = row['bars']+'#'+name
        else:
            data = read(row['path'])
            channels = channel_info(data)
            source_path = row['path']
        if any(c['codec'] != 1 or not 8000 <= c['rate'] <= 96000 for c in channels):
            rejected.append(dict(name=name, reason='Unsupported codec or sample rate'))
            continue
        category, side, peak, limit = classify(name)
        # For six-channel UI sounds use the same envelope per identity and deduplicate their starts.
        envelope_channel = max(channels, key=lambda c:c['stored_samples'])
        if envelope_channel['stored_samples'] < min(envelope_channel['full_samples'], math.ceil(limit*envelope_channel['rate']/1000)):
            rejected.append(dict(name=name,reason='Stored prefetch cannot cover the complete bounded pattern'))
            continue
        points = envelope(data, channels if category == 'START' else envelope_channel, peak, limit)
        if category == 'WATER_DASH':
            points = fine_water_texture(points)
        if category in ('SWING','THRUST'):
            points = attack_texture(points, category, name)
        prefixes = []
        ambiguous = False
        for info in channels:
            prefix = data[info['offset']:info['offset']+128]
            aliases = sources.get(prefix, [])
            if not aliases or any(classify(alias[0]) != classify(name) or alias[2] != info['rate'] for alias in aliases):
                ambiguous = True
                break
            prefixes.append(prefix)
        if ambiguous:
            rejected.append(dict(name=name,reason='Cross-cue or sample-rate alias'))
            continue
        chosen.append(dict(name=name, aliases=[name], category=category, locality=side, points=points,
                           prefixes=prefixes, banks=verified[name], source=source_path,
                           samples=envelope_channel['full_samples'],
                           source_sha256=hashlib.sha256(data).hexdigest()))
    # Identical prefixes of compatible effects (e.g. XL normal/finish) have one
    # cue identity. Unrelated aliases were rejected against the exhaustive map.
    unique = []
    by_prefix_set = {}
    for item in chosen:
        key = tuple(sorted(set(item['prefixes'])))
        existing = by_prefix_set.get(key)
        if existing is not None:
            assert classify(existing['name']) == classify(item['name'])
            existing['aliases'].extend(item['aliases'])
            existing['banks'] = sorted(set(existing['banks']+item['banks']))
        else:
            unique.append(item)
            by_prefix_set[key] = item
    chosen = unique
    assert any(x['category']=='START' for x in chosen)
    assert any(x['category']=='WATER_DASH' for x in chosen)
    assert any(x['category']=='CHARGE' for x in chosen)
    assert sum(x['category']=='CLIMB' for x in chosen)>50
    audio = ['/* Generated from the verified local game. Keep game-derived identities local. */',
             'static const struct { unsigned cue_id, category, locality; unsigned char prefix[128]; } extra_audio_fingerprints[] = {']
    patterns = ['/* Generated sound envelopes, sampled at 20 ms; original waveform stays local. */',
                '#include "haptic_cues.h"']
    rows = []
    category_ids = {'START':1,'DIALOGUE':2,'WATER_DASH':3,'CLIMB':4,'CHARGE':5,'SWING':6,'THRUST':7,'CLIMB_DASH':8}
    for cue_id, item in enumerate(chosen, 1):
        category = category_ids[item['category']]
        for prefix in sorted(set(item['prefixes'])):
            audio.append('    {%d,%d,%d,{%s}},' % (cue_id,category,item['locality'],','.join(str(x) for x in prefix)))
        patterns.append('static const TOTKHapticCuePoint extra_cue_points_%d[] = {' % cue_id)
        patterns.extend('    {%d,%.6ff,%.6ff},' % p for p in item['points'])
        patterns.append('};')
        rows.append('    {%d,extra_cue_points_%d,%d,%d},' % (cue_id,cue_id,len(item['points']),category==1))
    audio += ['};','#define EXTRA_AUDIO_FINGERPRINT_COUNT (sizeof extra_audio_fingerprints / sizeof extra_audio_fingerprints[0])']
    patterns += ['static const TOTKHapticCueDescriptor extra_cue_descriptors[] = {']+rows+['};',
                 '#define EXTRA_CUE_DESCRIPTOR_COUNT (sizeof extra_cue_descriptors / sizeof extra_cue_descriptors[0])']
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir/'gameplay_extra_audio.h').write_text('\n'.join(audio)+'\n')
    (output_dir/'gameplay_haptic_patterns.h').write_text('\n'.join(patterns)+'\n')
    report = dict(romfs_sha256=ROMFS_SHA, fingerprint_bytes=128, bars=bars_proof, rejected=rejected,
                  categories={key:sum(x['category']==key for x in chosen) for key in category_ids},
                  cues=[dict(cue_id=i,**{k:v for k,v in x.items() if k!='prefixes'},
                             prefix_sha256=[hashlib.sha256(p).hexdigest() for p in x['prefixes']])
                        for i,x in enumerate(chosen,1)],
                  limitations=['Audio identity carries no emitting actor pointer; gameplay cues require matching Player context.',
                               'Intensity and edge are designed from 20 ms sound RMS and first differences, not original haptic authoring.',
                               'No charge level or charge duration is invented; each real sound occurrence supplies its own pulse.'])
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    if emit:
        print(json.dumps(dict(cues=len(chosen),categories=report['categories'],rejected=len(rejected))))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('romfs', type=Path)
    parser.add_argument('output_dir', type=Path)
    parser.add_argument('report', type=Path)
    args = parser.parse_args()
    generate(args.romfs, args.output_dir, args.report)


if __name__ == '__main__':
    main()
