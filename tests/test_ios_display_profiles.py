"""Reject mismatched code-export and signed UIKit/metadata display profiles."""
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / 'games/totk/ios/tools'
with patch.object(sys, 'path', [str(TOOLS), *sys.path]):
    spec = importlib.util.spec_from_file_location('display_package_builder', TOOLS / 'build_ipa.py')
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)


class DisplayProfiles(unittest.TestCase):
    def test_duo_and_ipad_share_verified_narrow_preparation(self):
        for display in ('ipad', 'duo'):
            env = builder.display_export_env({}, Path('private-tree'), Path('private-modules'), 'v0', display)
            self.assertEqual(env['TOTK_ASPECT'], '4:3')
            self.assertEqual(env['TOTK_LIVE_DISPLAY'], '1')
            project = builder.display_project((ROOT / 'games/totk/ios/project.yml').read_text(), 27, 'v0', display)
            self.assertIn('TOTKLiveDisplayNarrow: true', project)
            self.assertIn('-DTOTK_LIVE_DISPLAY_NARROW=1', project)
            self.assertIn('CFBundleVersion: "27"', project)

    def test_wide_phone_cannot_inherit_narrow_or_relay_state(self):
        inherited = {key: '1' for key in ('TOTK_LIVE_DISPLAY', 'TOTK_LIVE_DISPLAY_AUDIT',
                                         'DM_DUO_RELAY', 'TOTK_IPAD_ASPECT_PATCH', 'TOTK_ASPECT_UI_PATCHED')}
        env = builder.display_export_env(inherited, Path('private-tree'), Path('private-modules'), 'v0', 'iphone')
        self.assertTrue(all(key not in env for key in inherited))
        self.assertEqual(env['TOTK_ASPECT'], '239:110')
        project = builder.display_project((ROOT / 'games/totk/ios/project.yml').read_text(), 27, 'v0', 'iphone')
        self.assertIn('TOTKLiveDisplayNarrow: false', project)
        self.assertIn('-DTOTK_LIVE_DISPLAY_NARROW=0', project)
        self.assertIn('TOTKDisplayAspect: "239:110"', project)

    def test_unqualified_version_and_profile_fail_closed(self):
        with self.assertRaisesRegex(RuntimeError, 'base 1.0.0'):
            builder.narrow_profile('v143', 'duo')
        with self.assertRaisesRegex(RuntimeError, 'unknown display'):
            builder.narrow_profile('v0', 'unqualified')


if __name__ == '__main__':
    unittest.main()
