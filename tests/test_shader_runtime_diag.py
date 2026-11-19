"""The legacy metallib directory log must not be read as a missing shader set."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
MTL = ROOT / 'games/totk/third_party/d2r_gfx/gfx/nvn/nvn_mtl.m'


class ShaderRuntimeDiag(unittest.TestCase):
    def test_legacy_directory_is_not_reported_as_a_missing_precompiled_set(self):
        source = MTL.read_text()
        self.assertNotIn('预编译 metallib 包 %s 不存在', source)
        self.assertIn('legacy metallib directory %s is absent', source)
        self.assertIn('source compilation runs only when that variant file is absent too', source)
        load = source.split('static NvShader *shader_load', 1)[1].split('static int stage_of', 1)[0]
        self.assertLess(load.find('rt_lib'), load.find('totk_msl_library'))
        self.assertIn('g_rt_prebuilt', load)
        get = source.split('static __unsafe_unretained id<MTLRenderPipelineState> pso_get', 1)[1]
        get = get.split('static NSString *warm_key', 1)[0]
        self.assertIn('newRenderPipelineStateWithDescriptor:d error:&e', get)


if __name__ == '__main__':
    unittest.main()
