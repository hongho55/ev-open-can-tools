import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class CanSelfTestContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = (ROOT / "include/app.h").read_text(encoding="utf-8")
        cls.dashboard = (ROOT / "include/web/mcp2515_dashboard.h").read_text(encoding="utf-8")
        cls.dual = (ROOT / "include/drivers/dual_can_driver.h").read_text(encoding="utf-8")
        cls.mcp = (ROOT / "include/drivers/esp32_mcp2515_driver.h").read_text(encoding="utf-8")
        cls.twai = (ROOT / "include/drivers/twai_driver.h").read_text(encoding="utf-8")

    def test_self_test_endpoint_is_authenticated_and_post_only(self):
        self.assertIn('server.on("/can_self_test", HTTP_POST, handleCanSelfTest);', self.dashboard)
        body = self.dashboard.split("static void handleCanSelfTest()", 1)[1].split("\n}", 1)[0]
        self.assertIn("server.authenticate(DASH_OTA_USER, DASH_OTA_PASS)", body)

    def test_maintenance_gate_blocks_normal_tx_and_clears_pending_work(self):
        self.assertIn("if (appMaintenanceTxInhibit)", self.app)
        body = self.dashboard.split("static void handleCanSelfTest()", 1)[1].split("\n}", 1)[0]
        self.assertIn("appMaintenanceTxInhibit = true;", body)
        self.assertIn("DashMaintenanceCleanupGuard cleanup(true);", body)
        self.assertIn("pluginResetPeriodicEmit();", body)
        self.assertIn("dashDriver->clearPendingTransmit();", body)
        self.assertIn("RuntimeDiagnostics::invalidateCanFreshness();", self.dashboard)

    def test_physical_drivers_recheck_policy_after_lock(self):
        self.assertIn("if (!sendAllowed(frame) || !initialized_)", self.mcp)
        self.assertIn("if (!sendAllowed(frame) || !driverOK_)", self.twai)
        self.assertIn("canA_.allowSendFrame = allowSendFrame;", self.dual)
        self.assertIn("canB_.allowSendFrame = allowSendFrame;", self.dual)

    def test_t2can_test_never_claims_twai_internal_loopback(self):
        self.assertIn('physicalTx\\\":false', self.dual)
        self.assertIn("controller_internal_loopback", self.mcp)
        self.assertIn("twai_has_no_isolated_internal_loopback", self.twai)
        self.assertNotIn("TWAI_MODE_NO_ACK", self.twai)


if __name__ == "__main__":
    unittest.main()
