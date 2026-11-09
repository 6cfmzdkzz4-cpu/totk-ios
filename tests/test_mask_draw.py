"""World-space gloom coverage is not a reason to retarget NVN clamp modes."""
from pathlib import Path
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
        self.assertIn('skipfs_', MTL)
        skip = MTL.index('uint64_t skip = atomic_load(&g_diag_skip_fs);')
        self.assertIn('if (skip && pr->sh[1]', MTL[skip:skip + 400])
        self.assertIn('setenv("TOTK_INPUT_FILE", commands.fileSystemRepresentation, 0);', MAIN)
        # The note runs only after a draw has already survived the missing-shader skip.
        miss = MTL.index('shader_miss_note(pr); g_draw_skip[15]++; return;')
        note = MTL.index('mask_draw_note(pr);')
        self.assertLess(miss, note)


if __name__ == '__main__':
    unittest.main()
