#!/usr/bin/env python3
"""Read-only base-1.0.0 Autobuild cost and tint classification.

The installed app is not modified. This tool never writes the game image, never
prints instruction words, and never treats a store of zero as a Zonaite cost.
A pchtxt address is a module memory offset. The NSO header is 0x100 bytes and
is not added. IPS offset_shift 0x100 does not apply to pchtxt lines.

  python3 games/totk/scripts/diag_autobuild_static.py \\
      --main extracted/v0/exefs/main \\
      --romfs-index extracted/v0/romfs/index.json \\
      --romfs-bin extracted/v0/romfs/romfs.bin

--main must be the regular extracted base-1.0.0 ExeFS file. The file SHA-256
must match games/totk/assets/expected-v1.0.0.json. A compressed NSO needs the
local lz4 package already used by prepare_modules.py. Symlinks, NSP/XCI/NSZ,
keys and saves are refused. --romfs-bin is hashed in a stream and not retained.
Paste the JSON back. It is the report: counts, match/mismatch, and classes.
Stage 1 schema is autobuild-static-1. Candidates stay null.
separation_possible is unknown. A store of zero at offset 0x48 is not a cost.
name_xrefs roles are ui_name_only.

Stage 2 reclassifies only the claimed function, its direct calls, AutoBuilder
string role tokens, and AutoBuilder path role counts. It does not print
instruction words, strings, or paths.

  python3 games/totk/scripts/diag_autobuild_static.py --stage 2 \\
      --main extracted/v0/exefs/main \\
      --romfs-index extracted/v0/romfs/index.json \\
      --romfs-bin extracted/v0/romfs/romfs.bin

Exit 0 when the pinned image was classified. Exit 2 when the image was not a
verified base 1.0.0 main. --fixture is only for the source test.
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import os
import stat
import struct
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = ROOT / 'assets' / 'expected-v1.0.0.json'
SHADER_LIST = ROOT / 'assets' / 'shader_list.tsv'
NSO_HEADER = 0x100
MAX_FILE = 256 * 1024 * 1024
MAX_XREFS = 16
MAX_PATHS = 24
CLASSIFY_CAP = 512
BACKSCAN = 256
# The supplied 1.0.0 green file names this module id and this memory offset.
# It is a claim to check, not a site to patch. 1.1.x cost addresses are absent
# on purpose: they are not 1.0.0 sites.
GREEN_CLAIM_BUILD_ID = '082ce09b06e33a123cb1e2770f5f9147709033db'
GREEN_CLAIM_ADDRESS = 0x161A788
GREEN_CLAIM_COUNT = 4
# Derived from the verified stage-1 run. Used only as a match check.
PRIOR_GREEN_FUNCTION_START = 0x161A730
PRIOR_GREEN_FUNCTION_INSNS = 54
PRIOR_CLAIM_INDEX = 22
PRIOR_UI_FUNCTION_START = 0x21249E4
PRIOR_UI_XREF_SITE = 0x2124AB8
CALLEE_CAP = 128
ROLE_TOKENS = (
    'Actor', 'Material', 'Shader', 'Layout', 'Energy', 'Battery', 'Draft',
    'History', 'Favorite', 'Cost', 'Effect', 'Anim', 'Model',
)
STRINGS = (
    {'key': 'AutoBuilder', 'basis': 'repository UI layout prefix', 'role': 'ui_name'},
    {'key': 'AutoBuilder_00', 'basis': 'repository AutoBuilder_00 layout', 'role': 'ui_name'},
)
PATTERNS = ('AutoBuilder', 'Zonau', 'Zonaite')
REFUSED_SUFFIXES = {'.nsp', '.xci', '.nsz', '.xcz', '.nca', '.keys', '.key', '.sav', '.npdm'}


def classify_word(word: int) -> dict:
    """Instruction class only. The word itself is not returned."""
    if word == 0xD503201F:
        return {'class': 'nop'}
    if word == 0xD65F03C0:
        return {'class': 'ret'}
    if word & 0xFC000000 == 0x94000000:
        return {'class': 'bl'}
    if word & 0xFC000000 == 0x14000000:
        return {'class': 'b'}
    if word & 0xFF000010 == 0x54000000:
        return {'class': 'b_cond'}
    if word & 0x7E000000 == 0x34000000:
        return {'class': 'cbz'}
    if word & 0x7E000000 == 0x36000000:
        return {'class': 'tbz'}
    if word & 0x9F000000 == 0x90000000:
        return {'class': 'adrp'}
    if word & 0x9F000000 == 0x10000000:
        return {'class': 'adr'}
    if word & 0xFFC00000 == 0x39000000:
        return _store('strb_imm', word, 1)
    if word & 0xFFC00000 == 0x79000000:
        return _store('strh_imm', word, 2)
    if word & 0xFFC00000 == 0xB9000000:
        return _store('str_w_imm', word, 4)
    if word & 0xFFC00000 == 0xF9000000:
        return _store('str_x_imm', word, 8)
    if word & 0xFF800000 == 0x52800000:
        return {'class': 'movz_w', 'immediate': (word >> 5) & 0xFFFF}
    if word & 0xFF00001F == 0x7100001F:
        return {'class': 'cmp_w_imm'}
    if word & 0xFF00001F == 0xF100001F:
        return {'class': 'cmp_x_imm'}
    if word & 0xFFE0001F == 0xD4000001:
        return {'class': 'svc'}
    return {'class': 'other'}


def _store(kind: str, word: int, scale: int) -> dict:
    source = word & 31
    zero = source == 31
    name = kind
    if zero and kind == 'str_w_imm':
        name = 'str_wzr_imm'
    elif zero and kind == 'str_x_imm':
        name = 'str_xzr_imm'
    return {
        'class': name, 'base': (word >> 5) & 31, 'offset': ((word >> 10) & 0xFFF) * scale,
        'source_zero': zero,
    }


def classify_window(words: list[int]) -> dict:
    rows = [classify_word(w) for w in words]
    counts = Counter(row['class'] for row in rows)
    stores = [row for row in rows if row['class'].startswith('str')]
    control = {'b', 'bl', 'b_cond', 'cbz', 'tbz', 'ret'}
    return {
        'instruction_count': len(words),
        'class_counts': dict(sorted(counts.items())),
        'all_nop': bool(rows) and all(row['class'] == 'nop' for row in rows),
        'has_control_transfer': any(row['class'] in control for row in rows),
        'store_shapes': [
            {key: row[key] for key in ('class', 'base', 'offset', 'source_zero')} for row in stores
        ],
        'semantic': 'unresolved',
    }


def interpret_green(window: dict) -> dict:
    """The supplied edit replaces this window with NOPs. That does not prove a tint-only change."""
    return {
        'replacement_class': 'nop_sequence',
        'supplied_patch_shows_appearance_isolation': False,
        'separation_possible': 'unknown',
        'marker_retained_by_this_edit': 'unknown',
        'original': window,
        'reason': (
            'The supplied edit deletes this window, including its branch. Stage-1 class '
            'other is not a flag store and is not a tint constant. separation_possible stays unknown.'
        ),
    }


def _logical_bits(sf: int, n: int, immr: int, imms: int) -> int | None:
    size = 64 if sf else 32
    combined = ((n & 1) << 6) | ((~imms) & 0x3F)
    length = next((bit for bit in range(6, -1, -1) if combined & (1 << bit)), 0)
    if length < 1:
        return None
    levels = (1 << length) - 1
    s, r = imms & levels, immr & levels
    if s == levels:
        return None
    bits = (1 << (s + 1)) - 1
    esize = 1 << length
    if r:
        bits = ((bits >> r) | (bits << (esize - r))) & ((1 << esize) - 1)
    while esize < size:
        bits |= bits << esize
        esize *= 2
    return bits & ((1 << size) - 1)


def _small(value: int) -> dict:
    if 0 <= value <= 64:
        return {'small_immediate': value}
    return {'immediate_above_64': True}


def detail_word(word: int) -> dict:
    """Finer class, registers, and small immediates. The word is not returned."""
    row = {'class': 'other', 'dests': [], 'uses': [], 'flags_write': False, 'flags_read': False,
           'mem': None, 'ctrl': None, 'clobbers_caller_saved': False}
    if word == 0xD503201F:
        row['class'] = 'nop'
        return row
    if word & 0xFFFFFC1F == 0xD65F0000:
        reg = (word >> 5) & 31
        row['class'] = 'ret'
        row['uses'] = [reg]
        row['ctrl'] = {'op': 'ret', 'reg': reg}
        return row
    if word & 0xFFFFFC1F == 0xD63F0000:
        row['class'] = 'blr'
        row['uses'] = [(word >> 5) & 31]
        row['clobbers_caller_saved'] = True
        row['ctrl'] = {'op': 'blr', 'reg': (word >> 5) & 31}
        return row
    if word & 0xFFFFFC1F == 0xD61F0000:
        row['class'] = 'br'
        row['uses'] = [(word >> 5) & 31]
        row['ctrl'] = {'op': 'br', 'reg': (word >> 5) & 31}
        return row
    if word & 0xFC000000 == 0x94000000:
        row['class'] = 'bl'
        row['clobbers_caller_saved'] = True
        row['ctrl'] = {'op': 'bl'}
        return row
    if word & 0xFC000000 == 0x14000000:
        row['class'] = 'b'
        row['ctrl'] = {'op': 'b'}
        return row
    if word & 0xFF000010 == 0x54000000:
        row['class'] = 'b_cond'
        row['flags_read'] = True
        row['ctrl'] = {'op': 'b_cond'}
        return row
    if word & 0x7F000000 in (0x34000000, 0x35000000):
        reg = word & 31
        row['class'] = 'cbz' if word & 0x01000000 == 0 else 'cbnz'
        row['uses'] = [reg]
        row['ctrl'] = {'op': row['class'], 'reg': reg}
        return row
    if word & 0x7F000000 in (0x36000000, 0x37000000):
        reg = word & 31
        row['class'] = 'tbz' if word & 0x01000000 == 0 else 'tbnz'
        row['uses'] = [reg]
        row['ctrl'] = {'op': row['class'], 'reg': reg}
        return row
    if word & 0x9F000000 == 0x90000000:
        row['class'] = 'adrp'
        row['dests'] = [word & 31]
        return row
    if word & 0x9F000000 == 0x10000000:
        row['class'] = 'adr'
        row['dests'] = [word & 31]
        return row
    if word & 0x3B000000 == 0x18000000:
        row['class'] = 'ldr_literal'
        row['dests'] = [word & 31]
        return row
    pair = _pair(word)
    if pair is not None:
        return pair
    mem = _mem(word)
    if mem is not None:
        row['class'] = mem.pop('class')
        row['mem'] = mem
        uses = [mem['base']]
        if mem.get('index_reg') is not None:
            uses.append(mem['index_reg'])
        if mem['op'] == 'load':
            row['dests'] = [mem['source_reg']]
        else:
            uses.append(mem['source_reg'])
        row['uses'] = uses
        return row
    logical = _logical(word)
    if logical is not None:
        return logical
    arith = _arith(word)
    if arith is not None:
        return arith
    if word & 0x1F800000 == 0x13000000:
        row['class'] = 'bitfield'
        row['dests'] = [word & 31]
        row['uses'] = [(word >> 5) & 31]
        return row
    if word & 0x3FE00C00 == 0x1A800000:
        row['class'] = 'csel'
        row['dests'] = [word & 31]
        row['uses'] = [(word >> 5) & 31, (word >> 16) & 31]
        row['flags_read'] = True
        return row
    if word & 0x1F000000 in (0x0E000000, 0x1E000000):
        row['class'] = 'float_or_simd'
        return row
    return row


def _pair(word: int) -> dict | None:
    if word & 0x7E000000 != 0x28000000:
        return None
    kind = 'ldp' if word & 0x00400000 else 'stp'
    rt, rn, rt2 = word & 31, (word >> 5) & 31, (word >> 10) & 31
    row = {'class': kind, 'dests': [], 'uses': [rn], 'flags_write': False, 'flags_read': False,
           'mem': {'op': 'load' if kind == 'ldp' else 'store', 'base': rn}, 'ctrl': None,
           'clobbers_caller_saved': False}
    if kind == 'ldp':
        row['dests'] = [rt, rt2]
    else:
        row['uses'] = [rn, rt, rt2]
    return row


def _mem(word: int) -> dict | None:
    widths = {0: 1, 1: 2, 2: 4, 3: 8}
    if word & 0x3B000000 == 0x39000000 and word & 0x01000000:
        scale = widths[(word >> 30) & 3]
        source = word & 31
        return {
            'class': ('ldr' if word & 0x00400000 else 'str') + '_imm',
            'op': 'load' if word & 0x00400000 else 'store', 'width': scale,
            'base': (word >> 5) & 31, 'offset': ((word >> 10) & 0xFFF) * scale,
            'source_reg': source, 'source_zero': source == 31, 'index_reg': None,
        }
    if word & 0x3B200C00 == 0x38200800:
        scale = widths[(word >> 30) & 3]
        source = word & 31
        return {
            'class': ('ldr' if word & 0x00400000 else 'str') + '_reg',
            'op': 'load' if word & 0x00400000 else 'store', 'width': scale,
            'base': (word >> 5) & 31, 'offset': None, 'source_reg': source,
            'source_zero': source == 31, 'index_reg': (word >> 16) & 31,
        }
    if word & 0x3B200C00 == 0x38000000:
        scale = widths[(word >> 30) & 3]
        source = word & 31
        imm9 = (word >> 12) & 0x1FF
        if imm9 & 0x100:
            imm9 -= 0x200
        return {
            'class': ('ldr' if word & 0x00400000 else 'str') + '_unscaled',
            'op': 'load' if word & 0x00400000 else 'store', 'width': scale,
            'base': (word >> 5) & 31, 'offset': imm9 * scale, 'source_reg': source,
            'source_zero': source == 31, 'index_reg': None,
        }
    return None


def _logical(word: int) -> dict | None:
    if word & 0x1F800000 == 0x12000000:
        sf, n = word >> 31, (word >> 22) & 1
        bits = _logical_bits(sf, n, (word >> 16) & 0x3F, (word >> 10) & 0x3F)
        if bits is None:
            return None
        opc = (word >> 29) & 3
        name = ('and_imm', 'orr_imm', 'eor_imm', 'ands_imm')[opc]
        dest, src = word & 31, (word >> 5) & 31
        row = {'class': name, 'dests': [] if dest == 31 and opc == 3 else [dest], 'uses': [src],
               'flags_write': opc == 3, 'flags_read': False, 'mem': None, 'ctrl': None,
               'clobbers_caller_saved': False, 'logical_bit_count': bits.bit_count(),
               'logical_single_bit': bits.bit_count() == 1}
        return row
    if word & 0x1F000000 == 0x0A000000:
        opc = (word >> 29) & 3
        name = ('and_reg', 'orr_reg', 'eor_reg', 'ands_reg')[opc]
        dest, src, rm = word & 31, (word >> 5) & 31, (word >> 16) & 31
        if opc == 1 and src == 31 and (word >> 10) & 0x3F == 0 and (word >> 22) & 3 == 0:
            name = 'mov_reg'
        row = {'class': name, 'dests': [] if dest == 31 and opc == 3 else [dest],
               'uses': [rm] if name == 'mov_reg' else [src, rm], 'flags_write': opc == 3,
               'flags_read': False, 'mem': None, 'ctrl': None, 'clobbers_caller_saved': False}
        return row
    return None


def _arith(word: int) -> dict | None:
    if word & 0x1F800000 == 0x11000000:
        opc = (word >> 29) & 3
        name = ('add_imm', 'adds_imm', 'sub_imm', 'subs_imm')[opc]
        dest, src = word & 31, (word >> 5) & 31
        shift = 12 if (word >> 22) & 1 else 0
        value = ((word >> 10) & 0xFFF) << shift
        if opc == 3 and dest == 31:
            name = 'cmp_imm'
        row = {'class': name, 'dests': [] if dest == 31 else [dest], 'uses': [src],
               'flags_write': opc in (1, 3), 'flags_read': False, 'mem': None, 'ctrl': None,
               'clobbers_caller_saved': False}
        if opc in (0, 2) or name == 'cmp_imm':
            row.update(_small(value))
        return row
    if word & 0x1F000000 == 0x0B000000:
        opc = (word >> 29) & 3
        name = ('add_reg', 'adds_reg', 'sub_reg', 'subs_reg')[opc]
        dest, src, rm = word & 31, (word >> 5) & 31, (word >> 16) & 31
        if opc == 3 and dest == 31:
            name = 'cmp_reg'
        return {'class': name, 'dests': [] if dest == 31 else [dest], 'uses': [src, rm],
                'flags_write': opc in (1, 3), 'flags_read': False, 'mem': None, 'ctrl': None,
                'clobbers_caller_saved': False}
    if word & 0x1F800000 == 0x12800000:
        opc = (word >> 29) & 3
        name = ('movn', None, 'movz', 'movk')[opc]
        if name is None:
            return None
        shift = ((word >> 21) & 3) * 16
        value = ((word >> 5) & 0xFFFF) << shift
        row = {'class': name, 'dests': [word & 31], 'uses': [word & 31] if name == 'movk' else [],
               'flags_write': False, 'flags_read': False, 'mem': None, 'ctrl': None,
               'clobbers_caller_saved': False, 'logical_bit_count': value.bit_count(),
               'logical_single_bit': value.bit_count() == 1}
        row.update(_small(value if shift == 0 else value))
        return row
    return None


def parse_header(raw: bytes) -> dict:
    if len(raw) < NSO_HEADER or raw[:4] != b'NSO0':
        raise ValueError('not an NSO0 file')
    flags = struct.unpack_from('<I', raw, 0xC)[0]
    segments = []
    for index, name in enumerate(('text', 'rodata', 'data')):
        fileoff, address, size, extra = struct.unpack_from('<IIII', raw, 0x10 + 16 * index)
        compressed = bool(flags & (1 << index))
        stored = struct.unpack_from('<I', raw, 0x60 + 4 * index)[0] if compressed else size
        if fileoff + stored > len(raw) or size > 256 * 1024 * 1024:
            raise ValueError(f'{name} segment is outside the file or over the size bound')
        segments.append({
            'name': name, 'file_offset': fileoff, 'address': address, 'size': size,
            'stored': stored, 'compressed': compressed, 'extra': extra,
            'hash_present': bool(flags & (8 << index)),
        })
    return {
        'flags': flags,
        'build_id': raw[0x40:0x54].hex(),
        'segments': segments,
        'bss': struct.unpack_from('<I', raw, 0x3C)[0],
    }


def memory_image(raw: bytes, header: dict) -> bytearray:
    end = max(seg['address'] + seg['size'] for seg in header['segments'])
    if end > 512 * 1024 * 1024:
        raise ValueError('image size is outside the read-only bound')
    image = bytearray(end)
    verified = []
    for index, seg in enumerate(header['segments']):
        payload = raw[seg['file_offset']:seg['file_offset'] + seg['stored']]
        if seg['compressed']:
            try:
                import lz4.block
            except ModuleNotFoundError as exc:
                raise ValueError('compressed NSO needs lz4 locally; nothing was written') from exc
            payload = lz4.block.decompress(payload, uncompressed_size=seg['size'])
        if len(payload) != seg['size']:
            raise ValueError(f'{seg["name"]} decompressed size differs')
        digest = hashlib.sha256(payload).digest()
        if seg['hash_present']:
            expect = raw[0xA0 + 32 * index:0xC0 + 32 * index]
            if digest != expect:
                raise ValueError(f'{seg["name"]} segment hash differs')
            verified.append(seg['name'])
        image[seg['address']:seg['address'] + seg['size']] = payload
    header['segment_hashes_verified'] = verified
    return image


def resolve_address(header: dict, memory_offset: int) -> dict:
    found = None
    for seg in header['segments']:
        if seg['address'] <= memory_offset < seg['address'] + seg['size']:
            found = seg
            break
    within = None if found is None else memory_offset - found['address']
    file_offset = None
    if found is not None and not found['compressed']:
        file_offset = found['file_offset'] + within
    return {
        'pchtxt_memory_offset': hex(memory_offset),
        'nso_header_size': NSO_HEADER,
        'ips_offset_shift_applied': False,
        'segment': None if found is None else found['name'],
        'offset_within_segment': None if within is None else hex(within),
        'raw_file_offset': None if file_offset is None else hex(file_offset),
        'compressed_segment': None if found is None else found['compressed'],
        'inside_initialized_segment': found is not None,
    }


def _words(image: bytearray, address: int, count: int, text: dict) -> list[int] | None:
    if address % 4 or count < 1:
        return None
    if address < text['address'] or address + 4 * count > text['address'] + text['size']:
        return None
    return list(struct.unpack_from(f'<{count}I', image, address))


def _adrp_target(image: bytearray, at: int) -> int | None:
    word = struct.unpack_from('<I', image, at)[0]
    if word & 0x9F000000 != 0x90000000:
        return None
    reg = word & 31
    imm = ((word >> 29) & 3) | (((word >> 5) & 0x7FFFF) << 2)
    page = (at & ~4095) + (((imm ^ (1 << 20)) - (1 << 20)) << 12)
    for step in range(1, 9):
        if at + 4 * step + 4 > len(image):
            return None
        follow = struct.unpack_from('<I', image, at + 4 * step)[0]
        if (follow >> 5) & 31 != reg:
            continue
        if follow & 0x7F800000 == 0x11000000:
            return page + ((follow >> 10) & 0xFFF)
        if follow & 0x3B000000 == 0x39000000:
            return page + ((follow >> 10) & 0xFFF) * (1 << (follow >> 30))
        return None
    return None


def _xrefs(image: bytearray, text: dict, targets: set[int]) -> dict[int, list[int]]:
    found = {target: [] for target in targets}
    if not targets:
        return found
    start, end = text['address'], text['address'] + text['size']
    for at in range(start, end - 36, 4):
        if struct.unpack_from('<I', image, at)[0] & 0x9F000000 != 0x90000000:
            continue
        target = _adrp_target(image, at)
        if target in found and len(found[target]) < MAX_XREFS:
            found[target].append(at)
    return found


def _c_string_count(image: bytearray, seg: dict, needle: str) -> int:
    data = bytes(image[seg['address']:seg['address'] + seg['size']])
    count = 0
    for part in data.split(b'\0'):
        if not 1 <= len(part) <= 96 or any(b < 32 or b > 126 for b in part):
            continue
        if needle.encode() in part:
            count += 1
    return count


def _exact_strings(image: bytearray, seg: dict, key: str) -> list[int]:
    data = bytes(image[seg['address']:seg['address'] + seg['size']])
    token = key.encode() + b'\0'
    found, start = [], 0
    while len(found) < MAX_XREFS:
        at = data.find(token, start)
        if at < 0:
            break
        if at == 0 or data[at - 1] == 0:
            found.append(seg['address'] + at)
        start = at + 1
    return found


def _prologue(image: bytearray, address: int, text: dict) -> int | None:
    stop = max(text['address'], address - 4 * BACKSCAN)
    for at in range(address, stop - 4, -4):
        word = struct.unpack_from('<I', image, at)[0]
        rt, rn, rt2 = word & 31, (word >> 5) & 31, (word >> 10) & 31
        if rt == 29 and rn == 31 and rt2 == 30 and word & 0x7FC00000 == 0x29800000:
            return at
    return None


def _function_summary(image: bytearray, text: dict, address: int, starts: list[int] | None) -> dict:
    method = 'eh_frame_hdr'
    end = None
    if starts:
        index = bisect.bisect_right(starts, address) - 1
        start = None if index < 0 else starts[index]
        if start is not None and index + 1 < len(starts):
            end = starts[index + 1]
    else:
        method = 'prologue_backscan'
        start = _prologue(image, address, text)
    if start is None:
        return {'method': method, 'found': False}
    limit = CLASSIFY_CAP
    if end is not None:
        limit = min(limit, max(0, (end - start) // 4))
    words = _words(image, start, limit, text) or []
    summary = classify_window(words)
    summary.update({
        'method': method, 'found': True, 'start': hex(start),
        'classified_instructions': len(words),
        'truncated': end is None or (end - start) // 4 > len(words),
        'semantic': 'unresolved',
    })
    return summary


def _store_zero_count(image: bytearray, text: dict) -> int:
    count = 0
    start, end = text['address'], text['address'] + text['size']
    for at in range(start, end - 3, 4):
        row = classify_word(struct.unpack_from('<I', image, at)[0])
        if row['class'] == 'str_wzr_imm' and row['offset'] == 0x48:
            count += 1
    return count


def _symbols(image: bytearray) -> dict:
    if len(image) < 8:
        return {'status': 'mod0_absent', 'matching_export_count': 0, 'matching_import_count': 0}
    modoff = struct.unpack_from('<I', image, 4)[0]
    if modoff <= 0 or modoff + 4 > len(image) or image[modoff:modoff + 4] != b'MOD0':
        return {'status': 'mod0_absent', 'matching_export_count': 0, 'matching_import_count': 0}
    sys.path.insert(0, str(ROOT / 'third_party' / 'd2r'))
    import nsoimg
    names = {item['key'] for item in STRINGS}
    exports, imports = [], []
    try:
        rows = nsoimg.relocs(bytes(image))
    except Exception:
        return {'status': 'relocations_unreadable', 'matching_export_count': 0, 'matching_import_count': 0}
    for _where, _kind, name, value, _addend in rows:
        if not name or not any(key in name for key in names):
            continue
        if value is None:
            imports.append(name)
        else:
            exports.append(name)
    return {
        'status': 'checked',
        'matching_export_count': len(set(exports)),
        'matching_import_count': len(set(imports)),
        'matching_names': sorted(set(exports) | set(imports))[:MAX_XREFS],
    }


def _function_starts(image: bytearray, text_end: int) -> list[int] | None:
    modoff = struct.unpack_from('<I', image, 4)[0]
    if modoff <= 0 or modoff + 4 > len(image) or image[modoff:modoff + 4] != b'MOD0':
        return None
    sys.path.insert(0, str(ROOT / 'third_party' / 'd2r'))
    import nsoimg
    try:
        return nsoimg.func_starts(bytes(image), text_end)
    except Exception:
        return None


def source_findings() -> dict:
    shader_rows = 0
    if SHADER_LIST.is_file():
        shader_rows = sum(1 for line in SHADER_LIST.read_text(errors='replace').splitlines() if 'AutoBuilder' in line)
    return {
        'game_version': '1.0.0',
        'actor_name_pointer': 'main actor+0x210',
        'ui_member_in_repository': 'anim/AutoBuilder_00_In.bflan',
        'shader_list_autobuilder_rows': shader_rows,
        'dynsym_in_source': 'no AutoBuilder, Zonaite, or material symbol is defined in this tree',
        'nvn_blend_constant': 'renderer blend color, not an Autobuild material',
        'device_pchtxt': 'iOS does not apply a pchtxt; export checks original instruction words before any write',
        'cost_candidate_in_source': False,
        'appearance_candidate_in_source': False,
    }


def _open_nofollow(path: Path):
    if path.suffix.lower() in REFUSED_SUFFIXES:
        raise ValueError('refusing a container, key, or save path')
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    info = os.fstat(fd)
    if not stat.S_ISREG(info.st_mode):
        os.close(fd)
        raise ValueError('path is not a regular file')
    return fd


def romfs_report(index_path: Path | None, bin_path: Path | None, expect_sha: str | None) -> dict:
    report = {'checked': False}
    if bin_path is not None:
        digest = hashlib.sha256()
        fd = _open_nofollow(bin_path)
        try:
            while True:
                block = os.read(fd, 1024 * 1024)
                if not block:
                    break
                digest.update(block)
        finally:
            os.close(fd)
        actual = digest.hexdigest()
        report['bytes_sha_match'] = actual == expect_sha
        if actual != expect_sha:
            report['status'] = 'romfs_sha_mismatch'
            return report
    if index_path is None:
        report['status'] = 'index_not_provided' if bin_path is None else 'index_not_provided_after_hash'
        return report
    fd = _open_nofollow(index_path)
    try:
        blob = bytearray()
        while len(blob) <= MAX_FILE:
            block = os.read(fd, 1024 * 1024)
            if not block:
                break
            blob += block
        else:
            raise ValueError('romfs index is larger than the read-only bound')
        payload = json.loads(blob)
    finally:
        os.close(fd)
    files = payload.get('files', [])
    grouped = {}
    for needle in PATTERNS:
        paths = [row['path'] for row in files if needle in str(row.get('path', ''))]
        item = {'count': len(paths)}
        if needle == 'AutoBuilder':
            item['paths'] = paths[:MAX_PATHS]
            item['truncated'] = len(paths) > MAX_PATHS
        grouped[needle] = item
    report.update(checked=True, status='classified', bytes_verified=bool(report.get('bytes_sha_match')), names=grouped)
    return report


def _public_detail(row: dict) -> dict:
    kept = ('class', 'dests', 'uses', 'flags_write', 'flags_read', 'mem', 'ctrl',
            'logical_bit_count', 'logical_single_bit', 'small_immediate', 'immediate_above_64')
    return {key: row[key] for key in kept if key in row and row[key] is not None}


def _reach(details: list[dict]) -> list[dict]:
    last: dict[int, tuple[int, str]] = {}
    out = []
    for index, row in enumerate(details):
        item = _public_detail(row)
        item['index'] = index
        reg = None if not row['ctrl'] else row['ctrl'].get('reg')
        if reg is not None and row['class'] in {'cbz', 'cbnz', 'tbz', 'tbnz'}:
            prior = last.get(reg)
            item['prior_def_index'] = None if prior is None else prior[0]
            item['prior_def_class'] = None if prior is None else prior[1]
        out.append(item)
        for reg in row['dests']:
            last[reg] = (index, row['class'])
        if row['clobbers_caller_saved']:
            for reg in range(19):
                last[reg] = (index, 'caller_saved_clobber')
            last[30] = (index, 'link')
    return out


def _cstring(image: bytearray, address: int, limit: int) -> bytes | None:
    if address < 0 or address >= limit:
        return None
    end = image.find(b'\0', address, min(limit, address + 97))
    if end < 0:
        return None
    text = bytes(image[address:end])
    if not text or any(byte < 32 or byte > 126 for byte in text):
        return None
    return text


def _roles(text: str) -> list[str]:
    return [token for token in ROLE_TOKENS if token in text]


def _bl_target(word: int, address: int) -> int | None:
    if word & 0xFC000000 != 0x94000000:
        return None
    imm = word & 0x3FFFFFF
    if imm & 0x2000000:
        imm -= 0x4000000
    return address + imm * 4


def _span(image: bytearray, address: int, count: int) -> list[dict]:
    words = struct.unpack_from(f'<{count}I', image, address)
    rows = []
    for index, word in enumerate(words):
        row = detail_word(word)
        if row['class'] == 'bl':
            row['target'] = _bl_target(word, address + 4 * index)
        if row['class'] == 'adrp':
            row['page_target'] = _adrp_target(image, address + 4 * index)
        rows.append(row)
    return rows


def _function_bounds(starts: list[int] | None, address: int) -> tuple[int | None, int | None]:
    if not starts:
        return None, None
    index = bisect.bisect_right(starts, address) - 1
    if index < 0:
        return None, None
    end = starts[index + 1] if index + 1 < len(starts) else None
    return starts[index], end


def _callee_summary(image: bytearray, starts: list[int] | None, target: int, text: dict) -> dict:
    start, end = _function_bounds(starts, target)
    if start is None or end is None or not text['address'] <= start < end <= text['address'] + text['size']:
        return {'target': hex(target), 'classified': False, 'reason': 'bounds_unavailable'}
    count = (end - start) // 4
    item = {'target': hex(target), 'start': hex(start), 'instruction_count': count}
    if count <= 0 or count > CALLEE_CAP or (target - start) % 4:
        item.update(classified=False, reason='over_cap' if count > CALLEE_CAP else 'unaligned')
        return item
    details = _span(image, start, count)
    counts = Counter(row['class'] for row in details)
    item.update(classified=True, class_counts=dict(sorted(counts.items())),
                store_count=sum(row['mem'] is not None and row['mem']['op'] == 'store' for row in details),
                single_bit_count=sum(bool(row.get('logical_single_bit')) for row in details),
                float_count=counts.get('float_or_simd', 0), semantic='unresolved')
    return item


def _adrp_role(image: bytearray, target: int | None, rodata: dict) -> dict:
    if target is None:
        return {'kind': 'unresolved'}
    if not rodata['address'] <= target < rodata['address'] + rodata['size']:
        return {'kind': 'outside_rodata', 'target': hex(target)}
    text = _cstring(image, target, rodata['address'] + rodata['size'])
    if text is None:
        return {'kind': 'rodata_non_string', 'target': hex(target)}
    return {'kind': 'cstring', 'target': hex(target), 'length': len(text),
            'roles': _roles(text.decode()), 'names_autobuilder': b'AutoBuilder' in text,
            'names_zonau': b'Zonau' in text}


def string_role_counts(image: bytearray, rodata: dict) -> tuple[dict[str, int], list[int]]:
    data = bytes(image[rodata['address']:rodata['address'] + rodata['size']])
    counts = {token: 0 for token in ROLE_TOKENS}
    interesting = []
    start = 0
    while start < len(data):
        end = data.find(b'\0', start)
        if end < 0:
            break
        part = data[start:end]
        start = end + 1
        if not 1 <= len(part) <= 96 or any(byte < 32 or byte > 126 for byte in part) or b'AutoBuilder' not in part:
            continue
        text = part.decode()
        roles = _roles(text)
        for role in roles:
            counts[role] += 1
        if any(role in roles for role in ('Actor', 'Material', 'Shader', 'Energy', 'Battery', 'Draft', 'History', 'Favorite', 'Cost')):
            if len(interesting) < MAX_XREFS:
                interesting.append(rodata['address'] + (end - len(part)))
    return counts, interesting


def romfs_role_counts(paths: list[str]) -> dict:
    auto = [path for path in paths if 'AutoBuilder' in path]
    suffixes = Counter(Path(path).suffix.lower() for path in auto)
    roots = Counter(path.split('/', 1)[0] for path in auto)
    tokens = {token: sum(token in path for path in auto) for token in ROLE_TOKENS}
    return {
        'autobuilder_count': len(auto),
        'suffix_counts': dict(sorted(suffixes.items())),
        'root_counts': dict(sorted(roots.items())),
        'token_counts': tokens,
        'zonau_and_autobuilder': sum('Zonau' in path and 'AutoBuilder' in path for path in paths),
        'archives_not_opened': True,
    }


def conclude(window: list[dict], callees: list[dict], string_roles: dict, path_roles: dict | None) -> dict:
    leads = []
    if any((row.get('mem') or {}).get('op') == 'store' or row.get('logical_single_bit') or row['class'] == 'float_or_simd'
           for row in window):
        leads.append('green_window_data_op')
    if any(item.get('classified') and (item.get('store_count') or item.get('single_bit_count') or item.get('float_count'))
           for item in callees):
        leads.append('green_callee_data_op')
    if any(string_roles.get(token) for token in ('Material', 'Shader', 'Energy', 'Battery', 'Cost')):
        leads.append('autobuilder_string_role')
    if path_roles and any(path_roles['token_counts'].get(token) for token in ('Material', 'Shader', 'Energy', 'Battery', 'Cost')):
        leads.append('autobuilder_path_role')
    missing = []
    if 'green_window_data_op' not in leads:
        missing.append('claimed window has no recognized store, single-bit logical write, or float op')
    if not any(string_roles.get(token) for token in ('Cost', 'Energy', 'Battery')):
        missing.append('no AutoBuilder string role Cost, Energy, or Battery')
    if path_roles and not any(path_roles['token_counts'].get(token) for token in ('Material', 'Shader')):
        missing.append('no AutoBuilder path token Material or Shader; archives were not opened')
    if not leads:
        missing.append('no bounded static cost or tint site; a store of zero at offset 0x48 is not a map')
    return {
        'semantic': 'unresolved',
        'separation_possible': 'unknown',
        'tint_isolated_by_this_evidence': False,
        'cost_identified': False,
        'stop': not leads,
        'leads': leads,
        'missing': missing,
        'candidates': {'zero_zonaite_autobuild_cost': None, 'appearance_only_no_green': None},
    }


def analyze_stage2(raw: bytes, expect_sha: str, claim_build_id: str = GREEN_CLAIM_BUILD_ID,
                    claim_address: int = GREEN_CLAIM_ADDRESS, paths: list[str] | None = None) -> dict:
    actual = hashlib.sha256(raw).hexdigest()
    report = {'schema': 'autobuild-static-2', 'sha256_match': actual == expect_sha,
              'v1_1_addresses_consulted': False, 'store_zero_is_cost': False}
    if actual != expect_sha:
        report['status'] = 'sha_mismatch'
        report['bytes_read'] = False
        return report
    header = parse_header(raw)
    image = memory_image(raw, header)
    text, rodata = header['segments'][0], header['segments'][1]
    report.update(status='classified', build_id_match=header['build_id'] == claim_build_id,
                  build_id=header['build_id'], segment_hashes_verified=header['segment_hashes_verified'],
                  bss_bytes_not_scanned=header['bss'])
    if not report['build_id_match']:
        report.update(bytes_read=False, reason='claim_build_id_mismatch')
        return report
    mapping = resolve_address(header, claim_address)
    if mapping['segment'] != 'text':
        report.update(bytes_read=False, reason='claim_window_outside_text', mapping=mapping)
        return report
    report['bytes_read'] = True
    report['mapping'] = mapping
    starts = _function_starts(image, text['address'] + text['size'])
    start, end = _function_bounds(starts, claim_address)
    report['prior_eh_frame_start_match'] = start == PRIOR_GREEN_FUNCTION_START
    if start is None or end is None or (end - start) % 4 or not text['address'] <= start < claim_address < end:
        report['green_function'] = {'found': False, 'reason': 'eh_frame_bounds_unavailable'}
        details = _span(image, claim_address, GREEN_CLAIM_COUNT)
        report['green_window'] = _reach(details)
        report.update(_name_section(image, text, rodata, starts, None, paths))
        report['conclusion'] = conclude(report['green_window'], [], report['autobuilder_string_roles'], report['romfs_roles'])
        return report
    count = (end - start) // 4
    report['green_function'] = {
        'found': True, 'start': hex(start), 'instruction_count': count,
        'prior_instruction_count_match': count == PRIOR_GREEN_FUNCTION_INSNS,
    }
    if count > CLASSIFY_CAP:
        report['green_function']['classified'] = False
        report['green_function']['reason'] = 'over_cap'
        report.update(_name_section(image, text, rodata, starts, None, paths))
        report['conclusion'] = conclude([], [], report['autobuilder_string_roles'], report['romfs_roles'])
        return report
    details = _span(image, start, count)
    claim_index = (claim_address - start) // 4
    report['green_function']['claim_index'] = claim_index
    report['green_function']['prior_claim_index_match'] = claim_index == PRIOR_CLAIM_INDEX
    reached = _reach(details)
    counts = Counter(row['class'] for row in details)
    report['green_function']['class_counts'] = dict(sorted(counts.items()))
    report['green_window'] = reached[claim_index:claim_index + GREEN_CLAIM_COUNT]
    stores = []
    branches = []
    calls = []
    pointers = []
    for row, detail in zip(reached, details):
        if detail['mem'] and detail['mem']['op'] == 'store':
            stores.append({'index': row['index'], 'class': detail['class'], 'base': detail['mem']['base'],
                           'offset': detail['mem']['offset'], 'source_zero': detail['mem']['source_zero'],
                           'source_reg': detail['mem']['source_reg']})
        if detail['class'] in {'cbz', 'cbnz', 'tbz', 'tbnz'}:
            branches.append({key: row[key] for key in ('index', 'class', 'prior_def_index', 'prior_def_class') if key in row})
        if detail['class'] == 'bl' and detail.get('target') is not None:
            calls.append(_callee_summary(image, starts, detail['target'], text) | {'index': row['index']})
        if detail['class'] == 'adrp':
            pointers.append({'index': row['index'], **_adrp_role(image, detail.get('page_target'), rodata)})
    report['green_function'].update(stores=stores, branches=branches, calls=calls[:8], pointers=pointers[:8],
                                    semantic='unresolved')
    report.update(_name_section(image, text, rodata, starts, start, paths))
    report['conclusion'] = conclude(report['green_window'], calls, report['autobuilder_string_roles'], report['romfs_roles'])
    return report


def _name_section(image, text, rodata, starts, green_start, paths) -> dict:
    roles, interesting = string_role_counts(image, rodata)
    xref_map = _xrefs(image, text, set(interesting))
    role_xrefs = []
    for address in interesting:
        for site in xref_map.get(address, [])[:4]:
            fn, fn_end = _function_bounds(starts, site)
            role_xrefs.append({
                'site': hex(site), 'function_start': None if fn is None else hex(fn),
                'same_as_green_function': fn == green_start,
                'callee_like': None if fn is None or fn_end is None else _callee_summary(image, starts, fn, text),
            })
            if len(role_xrefs) >= MAX_XREFS:
                break
    exact = _exact_strings(image, rodata, 'AutoBuilder')
    ui_sites = _xrefs(image, text, set(exact)).get(exact[0], []) if len(exact) == 1 else []
    ui_start = None if not ui_sites else _function_bounds(starts, ui_sites[0])[0]
    return {
        'autobuilder_string_roles': roles,
        'role_xrefs': role_xrefs,
        'romfs_roles': None if paths is None else romfs_role_counts(paths),
        'ui_name': {
            'exact_count': len(exact),
            'xref_site': None if not ui_sites else hex(ui_sites[0]),
            'function_start': None if ui_start is None else hex(ui_start),
            'prior_site_match': bool(ui_sites) and ui_sites[0] == PRIOR_UI_XREF_SITE,
            'prior_function_match': ui_start == PRIOR_UI_FUNCTION_START,
            'same_as_green_function': ui_start == green_start,
            'semantic': 'unresolved',
        },
    }


def analyze(raw: bytes, expect_sha: str, claim_build_id: str = GREEN_CLAIM_BUILD_ID,
            claim_address: int = GREEN_CLAIM_ADDRESS) -> dict:
    actual = hashlib.sha256(raw).hexdigest()
    report = {
        'schema': 'autobuild-static-1',
        'sha256_match': actual == expect_sha,
        'source': source_findings(),
        'cost_reference': {
            'v1_0_0_patch_present': False,
            'v1_1_addresses_consulted': False,
            'store_zero_is_cost': False,
            'semantic': 'unresolved',
            'reason': (
                'No base-1.0.0 cost patch was supplied. A 32-bit store of zero at offset 0x48 '
                'is counted and is not classified as Zonaite, a menu price, a battery cost, '
                'history, favorites, or a mixed real/generated part.'
            ),
        },
    }
    if actual != expect_sha:
        report['status'] = 'sha_mismatch'
        report['green_claim'] = {'bytes_read': False, 'reason': 'sha_mismatch'}
        return report
    header = parse_header(raw)
    image = memory_image(raw, header)
    text, rodata = header['segments'][0], header['segments'][1]
    report['status'] = 'classified'
    report['build_id_match'] = header['build_id'] == claim_build_id
    report['build_id'] = header['build_id']
    report['segment_hashes_verified'] = header['segment_hashes_verified']
    report['bss_bytes_not_scanned'] = header['bss']
    report['address_mapping'] = {
        'pchtxt': 'module memory offset; NSO header 0x100 is not added',
        'green_claim': resolve_address(header, claim_address),
    }
    report['symbols'] = _symbols(image)
    green = {
        'claim_address': hex(claim_address),
        'claim_build_id_match': report['build_id_match'],
        'mapping': report['address_mapping']['green_claim'],
    }
    if not report['build_id_match']:
        green.update(bytes_read=False, reason='claim_build_id_mismatch')
    else:
        words = _words(image, claim_address, GREEN_CLAIM_COUNT, text)
        green['bytes_read'] = words is not None
        if words is None:
            green['reason'] = 'claim_window_outside_text'
        else:
            green['interpretation'] = interpret_green(classify_window(words))
            starts = _function_starts(image, text['address'] + text['size'])
            green['containing_function'] = _function_summary(image, text, claim_address, starts)
    report['green_claim'] = green
    report['unresolved_store_zero_u32_offset_0x48_count'] = _store_zero_count(image, text)
    hits = []
    addresses = {}
    for item in STRINGS:
        found = _exact_strings(image, rodata, item['key'])
        addresses[item['key']] = found
        hits.append({**item, 'exact_count': len(found), 'addresses': [hex(value) for value in found]})
    xref_map = _xrefs(image, text, {addr for values in addresses.values() for addr in values})
    starts = _function_starts(image, text['address'] + text['size'])
    for item in hits:
        sites = []
        for addr in addresses[item['key']]:
            for site in xref_map.get(addr, []):
                sites.append({
                    'site': hex(site),
                    'function': _function_summary(image, text, site, starts),
                })
        item['xref_count'] = sum(len(xref_map.get(addr, [])) for addr in addresses[item['key']])
        item['xrefs'] = sites[:MAX_XREFS]
        item['semantic'] = 'unresolved'
        item['cost_role'] = 'ui_name_only'
    report['name_xrefs'] = hits
    report['rodata_cstring_counts'] = {needle: _c_string_count(image, rodata, needle) for needle in PATTERNS}
    report['candidates'] = {
        'zero_zonaite_autobuild_cost': None,
        'appearance_only_no_green': None,
        'separation_possible': 'unknown',
    }
    return report


def _read_regular(path: Path) -> bytes:
    if path.name.lower() in {'prod.keys', 'title.keys'}:
        raise ValueError('refusing a container, key, or save path')
    fd = _open_nofollow(path)
    try:
        info = os.fstat(fd)
        if info.st_size > MAX_FILE:
            raise ValueError('main is larger than the read-only bound')
        return os.read(fd, info.st_size)
    finally:
        os.close(fd)


def _index_paths(path: Path) -> list[str]:
    fd = _open_nofollow(path)
    try:
        blob = bytearray()
        while len(blob) <= MAX_FILE:
            block = os.read(fd, 1024 * 1024)
            if not block:
                break
            blob += block
        else:
            raise ValueError('romfs index is larger than the read-only bound')
    finally:
        os.close(fd)
    payload = json.loads(blob)
    return [str(row.get('path', '')) for row in payload.get('files', [])]


def expected_hashes() -> tuple[str, str]:
    payload = json.loads(EXPECTED.read_text())
    return payload['exefs_sha256']['main'], payload['romfs']['sha256']


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--stage', type=int, choices=(1, 2), default=1)
    parser.add_argument('--main', type=Path)
    parser.add_argument('--romfs-index', type=Path)
    parser.add_argument('--romfs-bin', type=Path)
    parser.add_argument('--fixture', action='store_true', help='test-only pin override')
    parser.add_argument('--expect-sha', help='required with --fixture')
    parser.add_argument('--expect-romfs-sha')
    args = parser.parse_args(argv)
    pinned_main, pinned_romfs = expected_hashes()
    if args.fixture:
        if not args.expect_sha:
            parser.error('--fixture requires --expect-sha')
        pinned_main = args.expect_sha
        if args.expect_romfs_sha:
            pinned_romfs = args.expect_romfs_sha
    elif args.expect_sha or args.expect_romfs_sha:
        parser.error('hash overrides require --fixture')
    if args.main is None:
        json.dump({'schema': 'autobuild-static-1' if args.stage == 1 else 'autobuild-static-2',
                   'status': 'private_image_not_provided', 'source': source_findings()}, sys.stdout, indent=2)
        sys.stdout.write('\n')
        return 2
    try:
        raw = _read_regular(args.main)
        paths = None
        precheck = None
        if args.romfs_index or args.romfs_bin:
            precheck = romfs_report(args.romfs_index, args.romfs_bin, pinned_romfs)
            if args.stage == 2 and args.romfs_index and precheck.get('status') != 'romfs_sha_mismatch':
                paths = _index_paths(args.romfs_index)
        if args.stage == 1:
            report = analyze(raw, pinned_main)
            if precheck is not None:
                report['romfs'] = precheck
        else:
            report = analyze_stage2(raw, pinned_main, paths=paths)
            if precheck is not None:
                report['romfs_precheck'] = {key: value for key, value in precheck.items() if key != 'names'}
    except (OSError, ValueError, MemoryError, json.JSONDecodeError) as exc:
        json.dump({'schema': 'autobuild-static-1' if args.stage == 1 else 'autobuild-static-2',
                   'status': 'refused', 'reason': str(exc)}, sys.stdout, indent=2)
        sys.stdout.write('\n')
        return 2
    json.dump(report, sys.stdout, indent=2)
    sys.stdout.write('\n')
    return 0 if report.get('sha256_match') else 2


if __name__ == '__main__':
    sys.exit(main())
