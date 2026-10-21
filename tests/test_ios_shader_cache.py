"""A changed cached library must never acquire a fresh trusted manifest hash."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

TOOLS=Path(__file__).resolve().parents[1]/'games/totk/ios/tools'
with patch.object(sys,'path',[str(TOOLS),*sys.path]):
    spec=importlib.util.spec_from_file_location('ios_package_builder',TOOLS/'build_ipa.py')
    builder=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)


class ShaderCacheTests(unittest.TestCase):
    def setUp(self):
        temp=tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.seed=Path(temp.name)
        files={'synthetic_b.metal':b'authored source fixture','synthetic_b.metallib':b'authored library fixture'}
        for name,data in files.items():(self.seed/name).write_bytes(data)
        self.manifest={'ios_source_manifest_sha256':'source-fixture','ios_metallibs':1,
            'ios_metal_flags':' '.join(builder.METAL_FLAGS),
            'files':{name:hashlib.sha256(data).hexdigest() for name,data in files.items()}}
        self.write_manifest()

    def write_manifest(self):
        (self.seed/'seed-manifest.json').write_text(json.dumps(self.manifest))

    def reusable(self):
        return builder.reusable_seed(self.seed,'source-fixture',['synthetic_b.metal'])

    def test_matching_cache_is_reused(self):
        self.assertTrue(self.reusable())

    def test_changed_library_is_rebuilt(self):
        (self.seed/'synthetic_b.metallib').write_bytes(b'changed bytes')
        self.assertFalse(self.reusable())

    def test_unrecorded_library_is_not_admitted(self):
        del self.manifest['files']['synthetic_b.metallib']
        self.write_manifest()
        self.assertFalse(self.reusable())

    def test_extra_unrecorded_file_is_not_admitted(self):
        (self.seed/'extra.metallib').write_bytes(b'unrecorded library')
        self.assertFalse(self.reusable())

    def test_source_or_math_mode_change_invalidates_cache(self):
        self.assertFalse(builder.reusable_seed(self.seed,'changed-source',['synthetic_b.metal']))
        self.manifest['ios_metal_flags']='fast'
        self.write_manifest()
        self.assertFalse(self.reusable())


if __name__=='__main__':
    unittest.main()
