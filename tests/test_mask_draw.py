"""World-space gloom coverage is not a reason to retarget NVN clamp modes."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
MTL = (ROOT/'games/totk/third_party/d2r_gfx/gfx/nvn/nvn_mtl.m').read_text()
NVN = (ROOT/'games/totk/third_party/d2r_gfx/gfx/nvn/include/nvn.h').read_text()
MAIN = (ROOT/'games/totk/ios/Sources/main.m').read_text()


class MaskDraw(unittest.TestCase):
    def test_wrap_modes_stay_on_the_documented_mapping(self):
        self.assertIn('NVN_WRAP_MODE_CLAMP = 0x0', NVN)
        self.assertIn('NVN_WRAP_MODE_CLAMP_TO_BORDER = 0x5', NVN)
        self.assertIn('NVN_WRAP_MODE_CLAMP_TO_EDGE = 0x7', NVN)
        start = MTL.index('static MTLSamplerAddressMode wrapm')
        body = MTL[start:MTL.index('static id<MTLSamplerState> samp_by_id')]
        self.assertIn('case 0: case 7: return MTLSamplerAddressModeClampToEdge', body)
        self.assertIn('case 5: case 4: return MTLSamplerAddressModeClampToBorderColor', body)
        self.assertIn('case 1: return MTLSamplerAddressModeRepeat', body)
        self.assertNotIn('case 0: case 7: return MTLSamplerAddressModeClampToBorderColor', body)

    def test_mask_draw_is_opt_in_metadata(self):
        self.assertIn('if (!strcmp(name, "mask_draw"))', MTL)
        self.assertIn('[mask-draw] F%llu fs=%.16s', MTL)
        self.assertIn('[mask-draw] keys F%llu draws=%u unique=%d omitted=%d', MTL)
        self.assertIn('swz=%u%u%u%u vfmt=%#x', MTL)
        self.assertIn('blend=%#x', MTL)
        self.assertIn('mask_draw_note(pr)', MTL)
        self.assertIn('samp=repeat', MTL)
        self.assertIn('[mask-io] %s key=%.16s dst=%ux%u fmt=%#x src=%ux%u', MTL)
        self.assertIn('mask_io_note("dispatch"', MTL)
        self.assertIn('mask_io_note("copy-t2t"', MTL)
        self.assertIn('mask_io_note("copy-b2t"', MTL)
        self.assertIn('mask_io_note("clear"', MTL)
        for name in ('skipfs_clear', 'skippre_', 'skipseen', 'skiphalf', 'skipkeep_', 'skipn'):
            self.assertIn(name, MTL)
        self.assertIn('[mask-io] frame %llu writes=%d', MTL)
        self.assertIn('mask_io_note("clear-rt"', MTL)
        drop = MTL.index('diag_fs_skipped(pr->sh[1]->key, scene_target)')
        gate = MTL[drop - 400:drop]
        self.assertIn('d.width == 1920', gate)
        self.assertIn('d.height == 1440', gate)
        self.assertLess(drop, MTL.index('defer_take(dw, di)', drop))
        self.assertIn('setenv("TOTK_INPUT_FILE", commands.fileSystemRepresentation, 0);', MAIN)
        miss = MTL.index('shader_miss_note(pr); g_draw_skip[15]++; return;')
        note = MTL.index('mask_draw_note(pr);')
        self.assertLess(miss, note)

    def test_skip_rules_match_several_keys_prefixes_and_halves(self):
        source = ROOT/'tests/test_mask_skip.c'
        binary = Path('/tmp/totk-mask-skip')
        build = subprocess.run(
            ['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)],
            capture_output=True, text=True)
        self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
        run = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn('mask skip checks passed', run.stdout)


if __name__ == '__main__':
    unittest.main()
