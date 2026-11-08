"""The supplied 1.0.0 Free Autobuild cheat is checked, not applied."""
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'games/totk/scripts'))
import check_free_autobuild_sites as sites
import diag_autobuild_static as diag

SCRIPT = ROOT / 'games/totk/scripts/check_free_autobuild_sites.py'
REFERENCE = """[Free Autobuild]
040E0000 01714820 D503201F
040E0000 01717610 2A1F03E8
040E0000 00D46270 D503201F
"""


def nso(build_id: bytes, text: bytes) -> bytes:
    header = bytearray(0x100)
    header[:4] = b'NSO0'
    blobs = (text, b'\0' * 16, b'\0' * 16)
    addresses = (0, 0x2000, 0x3000)
    fileoff = 0x100
    for index, (blob, address) in enumerate(zip(blobs, addresses)):
        struct.pack_into('<IIII', header, 0x10 + 16 * index, fileoff, address, len(blob), 0)
        fileoff += len(blob)
    header[0x40:0x40 + len(build_id)] = build_id
    return bytes(header) + b''.join(blobs)


class FreeAutobuildSites(unittest.TestCase):
    def test_veneer_preserves_the_original_or_runs_the_supplied_write(self):
        compiler = __import__('shutil').which('clang') or __import__('shutil').which('cc')
        if not compiler:
            self.skipTest('A C compiler is required')
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / 'veneer'
            build = subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    f'-I{ROOT / "games/totk/runtime"}',
                                    str(ROOT / 'tests/test_free_autobuild_veneer.c'),
                                    '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_experimental_export_cannot_replace_the_production_app(self):
        import importlib.util
        tools = ROOT / 'games/totk/ios/tools'
        spec = importlib.util.spec_from_file_location('display_package_builder', tools / 'build_ipa.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        with self.assertRaisesRegex(RuntimeError, 'com.willspain.totk'):
            builder.experimental_free_autobuild('com.willspain.totk', 'Tears of the Kingdom', '', 'v0')
        self.assertEqual(builder.experimental_free_autobuild(
            'com.willspain.totk.freeautobuild', 'Tears of the Kingdom', '', 'v0'), 'TOTK Free Autobuild')
        env = builder.display_export_env({'TOTK_FREE_AUTOBUILD_EXPORT': '1'}, Path('tree'), Path('modules'), 'v0', 'duo')
        self.assertNotIn('TOTK_FREE_AUTOBUILD_EXPORT', env)
        trial = builder.display_export_env({}, Path('tree'), Path('modules'), 'v0', 'duo', experimental=True)
        self.assertEqual(trial['TOTK_FREE_AUTOBUILD_EXPORT'], '1')
        for site in (0x1714820, 0x1717610, 0xd46270):
            self.assertNotIn(site, sites.runtime_sites())
    def test_cheat_menu_stays_off_the_production_bundle_and_does_not_apply_writes(self):
        settings = (ROOT/'games/totk/ios/Sources/TOTKSettings.m').read_text()
        patches = (ROOT/'games/totk/runtime/game_patches.c').read_text()
        self.assertIn('com.willspain.totk.freeautobuild', settings)
        self.assertIn('experimentalCheats?4:3', settings)
        self.assertIn('@"Infinite items"', settings)
        self.assertIn('free_autobuild_ready()', settings)
        self.assertIn('@"TOTKFreeAutobuild"', settings)
        self.assertIn('Applies on the next launch', settings)
        self.assertNotIn('0x1714820', (ROOT/'games/totk/runtime/game_patches.c').read_text())
        for site in ('0x1714820', '0x1717610', '0xd46270'):
            self.assertNotIn(site, patches.lower())
    def test_replacements_are_nop_and_mov_w8_wzr(self):
        self.assertEqual(sites.replacement_class(0xD503201F), 'nop')
        self.assertEqual(sites.replacement_class(0x2A1F03E8), 'mov_w8_wzr')
        self.assertIn(0x8F6C60, sites.runtime_sites())
        self.assertIn(0x1968C2C, sites.runtime_sites())

    def test_reference_rejects_an_extra_write(self):
        sites.parse_reference(REFERENCE)
        with self.assertRaises(ValueError):
            sites.parse_reference(REFERENCE + '040E0000 00000010 D503201F\n')
        with self.assertRaises(ValueError):
            sites.parse_reference('04000000 01714820 D503201F\n')

    def test_memory_offset_is_not_shifted_and_words_stay_out_of_the_report(self):
        text = bytearray(0x1000)
        planted = 0x6B08015F
        struct.pack_into('<III', text, 0x100, planted, 0xD65F03C0, 0xD503201F)
        raw = nso(bytes.fromhex(sites.FULL_BUILD_ID), bytes(text))
        local = ((0x100, 0xD503201F, 'nop'), (0x104, 0x2A1F03E8, 'mov_w8_wzr'), (0x108, 0xD503201F, 'nop'))
        report = sites.assess(raw, hashlib.sha256(raw).hexdigest(), local)
        self.assertTrue(report['all_inside_text'])
        self.assertEqual([item['original_class'] for item in report['sites']], ['cmp_reg', 'ret', 'nop'])
        self.assertEqual(report['sites'][0]['memory_offset'], '0x100')
        self.assertNotIn('0x200', json.dumps(report['sites'][0]))
        self.assertFalse(report['apply'])
        self.assertFalse(report['cost_verified'])
        encoded = json.dumps(report)
        self.assertNotIn(f'{planted:08x}', encoded)
        self.assertNotIn('d503201f', encoded)
        self.assertEqual(report['original_words_sha256'], hashlib.sha256(struct.pack('<III', planted, 0xD65F03C0, 0xD503201F)).hexdigest())

    def test_prefix_alone_does_not_read_sites(self):
        raw = nso(bytes.fromhex('082ce09b06e33a12' + '00' * 12), b'\0' * 64)
        report = sites.assess(raw, hashlib.sha256(raw).hexdigest())
        self.assertTrue(report['build_id_prefix_match'])
        self.assertFalse(report['build_id_match'])
        self.assertFalse(report['bytes_read'])
        self.assertEqual(report['sites'], [])

    def test_runtime_patch_collision_and_cli(self):
        text = bytearray(0x200)
        raw = nso(bytes.fromhex(sites.FULL_BUILD_ID), bytes(text))
        local = ((0x8F6C60, 0xD503201F, 'nop'), (0x4, 0x2A1F03E8, 'mov_w8_wzr'), (0x8, 0xD503201F, 'nop'))
        # 0x8f6c60 is outside this tiny text, so the collision flag still records the known site.
        report = sites.assess(raw, hashlib.sha256(raw).hexdigest(), ((0x10, 0xD503201F, 'nop'),) * 1)
        self.assertFalse(report['sites'][0]['overlaps_runtime_patch'])
        occupied = sites.assess(raw, hashlib.sha256(raw).hexdigest(), ((0x8F6C60, 0xD503201F, 'nop'),))
        self.assertTrue(occupied['sites'][0]['overlaps_runtime_patch'])
        self.assertFalse(occupied['sites'][0]['inside_text'])
        with tempfile.TemporaryDirectory() as folder:
            main = Path(folder) / 'main'
            cheat = Path(folder) / '082CE09B06E33A12.txt'
            main.write_bytes(raw)
            cheat.write_text(REFERENCE)
            done = subprocess.run([
                sys.executable, str(SCRIPT), '--fixture', '--main', str(main),
                '--expect-sha', hashlib.sha256(raw).hexdigest(), '--reference', str(cheat),
            ], capture_output=True, text=True)
        self.assertEqual(done.returncode, 2, done.stderr)
        body = json.loads(done.stdout)
        self.assertEqual(body['reference_status'], 'sha_mismatch')
        self.assertFalse(body['apply'])
        self.assertFalse(body['cost_verified'])
        self.assertFalse(any(item['inside_text'] for item in body['sites']))


if __name__ == '__main__':
    unittest.main()
