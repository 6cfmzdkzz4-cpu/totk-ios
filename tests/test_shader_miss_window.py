"""Missing-shader draws stay skipped. The 300-frame report names the key and target only."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
MTL = (ROOT/'games/totk/third_party/d2r_gfx/gfx/nvn/nvn_mtl.m').read_text()
HLE = (ROOT/'games/totk/third_party/d2r_gfx/gfx/nvn/hle_nvn.c').read_text()


class ShaderMissWindow(unittest.TestCase):
    def test_skip_is_unchanged_and_named(self):
        self.assertIn('if (pr && pr->missing) { shader_miss_note(pr); g_draw_skip[15]++; return; }', MTL)
        self.assertIn('if (fit < 0) { shader_miss_note(pr); g_draw_skip[15]++; return; }', MTL)
        self.assertIn('#define SMISS_SLOTS 8', MTL)
        self.assertIn('visible=%llu gated=%llu target=%ux%u fmt=%#x', MTL)
        self.assertIn('s->draws - s->seen_draws', MTL)
        self.assertNotIn('code_hex', MTL[MTL.find('shader_miss_note'):MTL.find('static void draw_inner')])
        self.assertIn('mtl_shader_miss_window(w, sizeof w)', HLE)
        self.assertIn('g_draw_skip[i] - prev[i]', HLE)
        self.assertIn('"clear-render-disabled", "cond-disabled", "cond-unknown", "shader-missing"', HLE)

    def test_window_math(self):
        # Printed counts are the change since the previous report, and reports are every 300 frames.
        self.assertEqual(divmod(1342, 300), (4, 142))
        self.assertEqual(divmod(12213, 300), (40, 213))
        self.assertEqual(divmod(2328, 300), (7, 228))
        self.assertEqual(divmod(2088, 300), (6, 288))
        self.assertIn('g_draw_skip[12]++', MTL)
        self.assertIn('g_draw_skip[13]++', MTL)
        self.assertIn('g_draw_skip[14]++', MTL)


if __name__ == '__main__':
    unittest.main()
