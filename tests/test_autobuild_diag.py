"""Read-only Autobuild diagnostic. Synthetic NSO only; no game image."""
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'games/totk/scripts'))
import diag_autobuild_static as diag

SCRIPT = ROOT / 'games/totk/scripts/diag_autobuild_static.py'
CLAIM = bytes.fromhex(diag.GREEN_CLAIM_BUILD_ID)
STR_WZR_X9_0X48 = 0xB900493F
STR_WZR_X25_0X48 = 0xB9004B3F
NOP = 0xD503201F
PROLOGUE = 0xA9BF7BFD


def adrp_add(at, target, reg=0):
    page_delta = (target & ~4095) - (at & ~4095)
    imm = page_delta >> 12
    adrp = 0x90000000 | ((imm & 3) << 29) | ((imm >> 2) << 5) | reg
    add = 0x91000000 | ((target & 0xFFF) << 10) | (reg << 5) | reg
    return adrp, add


def nso(build_id, text, rodata, text_addr=0, ro_addr=0x2000):
    data_addr = 0x4000
    data = b'\0' * 16
    header = bytearray(0x100)
    header[:4] = b'NSO0'
    blobs = (text, rodata, data)
    addresses = (text_addr, ro_addr, data_addr)
    fileoff = 0x100
    for index, (blob, address) in enumerate(zip(blobs, addresses)):
        struct.pack_into('<IIII', header, 0x10 + 16 * index, fileoff, address, len(blob), 0)
        struct.pack_into('<I', header, 0x60 + 4 * index, len(blob))
        fileoff += len(blob)
    header[0x40:0x40 + len(build_id)] = build_id
    return bytes(header) + text + rodata + data


def image_with_sites(build_id):
    text = bytearray(0x1000)
    struct.pack_into('<I', text, 0x80, PROLOGUE)
    struct.pack_into('<II', text, 0x100, *adrp_add(0x100, 0x2000))
    struct.pack_into('<I', text, 0x180, STR_WZR_X9_0X48)
    struct.pack_into('<IIII', text, 0x200, 0x3900013F, 0x7900025F, NOP, 0xD65F03C0)
    rodata = bytearray(b'AutoBuilder_00\0AutoBuilder\0ZonauEnergyCell\0')
    return nso(build_id, bytes(text), bytes(rodata))


class AutobuildDiagnostic(unittest.TestCase):
    def test_store_zero_shape_is_unresolved_and_not_a_word(self):
        window = diag.classify_window([STR_WZR_X9_0X48, STR_WZR_X25_0X48])
        self.assertEqual(window['semantic'], 'unresolved')
        self.assertEqual(window['store_shapes'], [
            {'class': 'str_wzr_imm', 'base': 9, 'offset': 0x48, 'source_zero': True},
            {'class': 'str_wzr_imm', 'base': 25, 'offset': 0x48, 'source_zero': True},
        ])
        encoded = json.dumps(window)
        self.assertNotIn(f'{STR_WZR_X9_0X48:08x}', encoded)
        self.assertNotIn('3f4900b9', encoded)

    def test_nop_replacement_does_not_decide_separation(self):
        found = diag.interpret_green(diag.classify_window([NOP, NOP, NOP, NOP]))
        self.assertFalse(found['supplied_patch_shows_appearance_isolation'])
        self.assertEqual(found['separation_possible'], 'unknown')
        self.assertNotIn('impossible', json.dumps(found).lower())

    def test_pchtxt_address_is_memory_offset_without_header_shift(self):
        raw = image_with_sites(CLAIM)
        header = diag.parse_header(raw)
        mapped = diag.resolve_address(header, 0x100)
        self.assertEqual(mapped['pchtxt_memory_offset'], '0x100')
        self.assertFalse(mapped['ips_offset_shift_applied'])
        self.assertEqual(mapped['nso_header_size'], 0x100)
        self.assertEqual(mapped['segment'], 'text')
        self.assertEqual(mapped['raw_file_offset'], '0x200')

    def test_sha_mismatch_reads_no_window(self):
        report = diag.analyze(image_with_sites(CLAIM), '00' * 32)
        self.assertEqual(report['status'], 'sha_mismatch')
        self.assertFalse(report['green_claim']['bytes_read'])
        self.assertNotIn('ZonauEnergyCell', json.dumps(report))
        self.assertNotIn('store_shapes', json.dumps(report))

    def test_build_id_mismatch_does_not_classify_the_claim_address(self):
        raw = image_with_sites(b'\x11' * 20)
        report = diag.analyze(raw, hashlib.sha256(raw).hexdigest())
        self.assertTrue(report['sha256_match'])
        self.assertFalse(report['green_claim']['bytes_read'])
        self.assertEqual(report['green_claim']['reason'], 'claim_build_id_mismatch')
        self.assertIsNone(report['candidates']['zero_zonaite_autobuild_cost'])
        self.assertIsNone(report['candidates']['appearance_only_no_green'])
        self.assertEqual(report['unresolved_store_zero_u32_offset_0x48_count'], 1)
        names = {item['key']: item for item in report['name_xrefs']}
        self.assertEqual(names['AutoBuilder_00']['exact_count'], 1)
        self.assertEqual(names['AutoBuilder_00']['xref_count'], 1)
        self.assertEqual(names['AutoBuilder_00']['cost_role'], 'ui_name_only')
        self.assertEqual(names['AutoBuilder_00']['semantic'], 'unresolved')
        self.assertTrue(names['AutoBuilder_00']['xrefs'][0]['function']['found'])
        self.assertEqual(report['rodata_cstring_counts']['Zonau'], 1)
        self.assertNotIn('ZonauEnergyCell', json.dumps(report))
        self.assertNotIn(f'{STR_WZR_X9_0X48:08x}', json.dumps(report))

    def test_matching_claim_classifies_original_window_without_words(self):
        raw = image_with_sites(CLAIM)
        report = diag.analyze(raw, hashlib.sha256(raw).hexdigest(), claim_address=0x200)
        green = report['green_claim']
        self.assertTrue(green['bytes_read'])
        counts = green['interpretation']['original']['class_counts']
        self.assertEqual(counts['strb_imm'], 1)
        self.assertEqual(counts['strh_imm'], 1)
        self.assertEqual(counts['nop'], 1)
        self.assertEqual(counts['ret'], 1)
        self.assertEqual(green['interpretation']['separation_possible'], 'unknown')
        self.assertFalse(green['interpretation']['supplied_patch_shows_appearance_isolation'])
        encoded = json.dumps(report)
        self.assertNotIn('3900013f', encoded)
        self.assertNotIn('7900025f', encoded)

    def test_cli_refuses_a_key_path_and_emits_schema(self):
        with tempfile.TemporaryDirectory() as folder:
            key = Path(folder) / 'prod.keys'
            key.write_text('not a real key')
            done = subprocess.run([sys.executable, str(SCRIPT), '--main', str(key)], capture_output=True, text=True)
        self.assertEqual(done.returncode, 2)
        self.assertEqual(json.loads(done.stdout)['status'], 'refused')
        missing = subprocess.run([sys.executable, str(SCRIPT)], capture_output=True, text=True)
        self.assertEqual(missing.returncode, 2)
        self.assertEqual(json.loads(missing.stdout)['schema'], 'autobuild-static-1')
        self.assertFalse(json.loads(missing.stdout)['source']['cost_candidate_in_source'])

    def test_fixture_cli_and_romfs_counts(self):
        raw = image_with_sites(CLAIM)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            main = root / 'main'
            main.write_bytes(raw)
            index = root / 'index.json'
            index.write_text(json.dumps({'files': [
                {'path': 'UI/LayoutArchive/AutoBuilder_00.bflyt'},
                {'path': 'Model/Other.bfres'},
            ]}))
            blob = root / 'romfs.bin'
            blob.write_bytes(b'romfs-bytes')
            done = subprocess.run([
                sys.executable, str(SCRIPT), '--fixture', '--main', str(main),
                '--expect-sha', hashlib.sha256(raw).hexdigest(),
                '--romfs-index', str(index), '--romfs-bin', str(blob),
                '--expect-romfs-sha', hashlib.sha256(b'romfs-bytes').hexdigest(),
            ], capture_output=True, text=True)
            link = root / 'link'
            os.symlink(main, link)
            linked = subprocess.run([
                sys.executable, str(SCRIPT), '--fixture', '--main', str(link),
                '--expect-sha', hashlib.sha256(raw).hexdigest(),
            ], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stderr)
        report = json.loads(done.stdout)
        self.assertTrue(report['build_id_match'])
        self.assertFalse(report['green_claim']['bytes_read'])
        self.assertEqual(report['green_claim']['reason'], 'claim_window_outside_text')
        self.assertEqual(report['romfs']['names']['AutoBuilder']['count'], 1)
        self.assertEqual(report['romfs']['names']['Zonau']['count'], 0)
        self.assertNotIn('paths', report['romfs']['names']['Zonau'])
        self.assertEqual(linked.returncode, 2)
        self.assertEqual(json.loads(linked.stdout)['status'], 'refused')


if __name__ == '__main__':
    unittest.main()
