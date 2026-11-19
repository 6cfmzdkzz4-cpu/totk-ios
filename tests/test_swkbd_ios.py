"""iOS horse naming must show a dialog and return a name, not cancel."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SWKBD = (ROOT/'games/totk/runtime/swkbd_host.c').read_text()
IOS = (ROOT/'games/totk/runtime/frontend_ios.m').read_text()
INPUT = (ROOT/'games/totk/runtime/input.c').read_text()


class SwkbdIos(unittest.TestCase):
    def test_ios_presents_a_dialog_and_falls_back_to_a_name(self):
        self.assertIn('#if TARGET_OS_OSX || TARGET_OS_IPHONE', SWKBD)
        self.assertNotIn('text entry is not available on this platform; cancelled', SWKBD)
        self.assertIn('[swkbd] launch mode=%u max=%d initial=%s', SWKBD)
        self.assertIn('[swkbd] result=%s text=%s', SWKBD)
        self.assertIn('swkbd_default_name[64] = "Horse"', INPUT)
        self.assertIn('runtime_swkbd_default()', SWKBD)
        self.assertIn('dialog unavailable; using default', SWKBD)
        self.assertIn('text check requested; reply ok', SWKBD)
        self.assertIn('return confirmed ? 0 : SWKBD_CANCELED', SWKBD)
        self.assertIn('UIAlertControllerStyleAlert', IOS)
        self.assertIn('addTextFieldWithConfigurationHandler', IOS)
        self.assertIn('runtime_input_suppress(1)', IOS)
        self.assertIn('dispatch_semaphore_wait(sem,DISPATCH_TIME_FOREVER)', IOS)
        self.assertIn('swkbddefault ', INPUT)
        self.assertIn('runtime_swkbd_set_default', INPUT)
        self.assertIn('runtime_input_suppress', INPUT)


if __name__ == '__main__':
    unittest.main()
