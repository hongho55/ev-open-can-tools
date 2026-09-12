import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DASHBOARD = ROOT / "include/web/mcp2515_dashboard.h"


class VehicleDiscoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dashboard = DASHBOARD.read_text(encoding="utf-8")

    def test_discovery_contract_is_local_read_only_and_versioned(self):
        source = self.dashboard
        self.assertIn("kDashDiscoveryPort = 36991", source)
        self.assertIn('kDashDiscoveryRequest[] = "T2CAN_DISCOVER_V1"', source)
        self.assertIn('"schema\\\":\\\"t2can-discovery-v1', source)
        self.assertIn('"service\\\":\\\"EVCANTool', source)
        self.assertIn('"port\\\":80', source)
        self.assertIn('"readOnly\\\":true', source)

    def test_discovery_response_does_not_advertise_control_or_sensitive_data(self):
        start = source_start = self.dashboard.index("static constexpr char kDashDiscoveryResponse[]")
        end = self.dashboard.index("static int dashDiscoverySocket", start)
        response = self.dashboard[source_start:end]
        for forbidden in ("event_ack", "event_download", "password", "token", "inject", "OTA"):
            self.assertNotIn(forbidden, response)

    def test_discovery_is_non_blocking_and_bounded(self):
        start = self.dashboard.index("static void dashDiscoveryTick()")
        end = self.dashboard.index("#else", start)
        tick = self.dashboard[start:end]
        self.assertIn("O_NONBLOCK", self.dashboard)
        self.assertIn("attempt < 4", tick)
        self.assertIn("memcmp(request, kDashDiscoveryRequest", tick)
        self.assertIn("sendto(dashDiscoverySocket", tick)


if __name__ == "__main__":
    unittest.main()
