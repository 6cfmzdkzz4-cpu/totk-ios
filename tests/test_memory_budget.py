"""Host memory diagnostics do not raise the guest pool or add an entitlement."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MemoryBudget(unittest.TestCase):
    def test_budget_log_does_not_change_caps(self):
        services = (ROOT/'games/totk/runtime/services.c').read_text()
        entitlements = (ROOT/'games/totk/ios/Sources/TOTK.entitlements').read_text()
        ipa = (ROOT/'games/totk/ios/tools/build_ipa.py').read_text()
        self.assertIn('0xcd500000ULL', services)
        self.assertIn('#define HEAP_CAP (4ULL<<30)', services)
        self.assertIn('#if TARGET_OS_IPHONE\n#include <os/proc.h>', services)
        call = services.index('os_proc_available_memory()')
        self.assertIn('#if TARGET_OS_IPHONE', services[max(0, call - 400):call])
        self.assertIn('available_status="unavailable"', services)
        self.assertIn('long long available=-1', services)
        self.assertNotIn('available=0', services[services.index('host_budget_log'):services.index('host_pressure_event')])
        self.assertIn('info.phys_footprint', services)
        self.assertIn('info.resident_size', services)
        self.assertIn('available_status=%s', services)
        self.assertIn('[totk-mem] budget unix=', services)
        self.assertIn('host_budget_log("startup",0)', services)
        self.assertIn('host_budget_log("set_heap_refused",size)', services)
        self.assertIn('map_physical_failed', services)
        self.assertIn('pressure-critical', services)
        self.assertNotIn('^{', services)
        self.assertIn('com.apple.developer.kernel.increased-memory-limit', entitlements)
        self.assertNotIn('extended-virtual-addressing', entitlements)
        self.assertNotIn('extended-virtual-addressing', ipa)
        self.assertEqual(entitlements.count('<key>'), 1)


if __name__ == '__main__':
    unittest.main()
