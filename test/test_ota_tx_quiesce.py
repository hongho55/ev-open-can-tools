import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DASHBOARD = (ROOT / "include/web/mcp2515_dashboard.h").read_text(encoding="utf-8")


class OtaTxQuiesceTests(unittest.TestCase):
    def test_quiesce_preserves_persisted_enablement_and_clears_pending_tx(self):
        body = re.search(
            r"static void dashQuiesceTransmitForOta\(const char \*source\)\s*\{(.*?)\n\}",
            DASHBOARD,
            re.S,
        )
        if body is None:
            self.fail("dashQuiesceTransmitForOta definition missing")
        code = body.group(1)
        self.assertIn("appMaintenanceTxInhibit = true;", code)
        self.assertIn("pluginResetPeriodicEmit();", code)
        self.assertIn("dashDriver->clearPendingTransmit();", code)
        self.assertNotIn("dashSetCanActive(false", code)
        self.assertNotIn("canActive = false", code)

    def test_every_device_ota_entry_path_quiesces_before_update_begin(self):
        for source in ("web_upload", "web_install", "auto_update"):
            pattern = (
                rf'dashQuiesceTransmitForOta\("{source}"\);\s*'
                r"(?:DashMaintenanceCleanupGuard cleanup;\s*)?"
                r"(?:(?:manualOtaAccepted = )|(?:if \(!))Update\.begin\("
            )
            self.assertRegex(DASHBOARD, pattern, source)

    def test_arduino_ota_quiesces_at_start(self):
        self.assertRegex(
            DASHBOARD,
            r"ArduinoOTA\.onStart\(\[\]\(\)\s*\{\s*"
            r'dashQuiesceTransmitForOta\("arduino_ota"\);',
        )

    def test_failed_ota_releases_only_non_rollback_maintenance_inhibit(self):
        body = re.search(
            r"static void dashResumeTransmitAfterOtaFailure\(\)\s*\{(.*?)\n\}",
            DASHBOARD,
            re.S,
        )
        if body is None:
            self.fail("dashResumeTransmitAfterOtaFailure definition missing")
        code = body.group(1)
        self.assertIn("OtaBootState::Pending", code)
        self.assertIn("OtaBootState::Rollback", code)
        self.assertIn("appMaintenanceTxInhibit = false;", code)
        self.assertLess(code.index("OtaBootState::Pending"), code.index("appMaintenanceTxInhibit = false;"))
        self.assertLess(code.index("OtaBootState::Rollback"), code.index("appMaintenanceTxInhibit = false;"))
        for failure_marker in (
            'dashLog("[OTA] Begin failed")',
            'dashLog("[OTA] Write error")',
            'dashLog("[OTA] Upload aborted")',
            'dashLog("[AUTO-OTA] Finalize failed:',
        ):
            marker_pos = DASHBOARD.find(failure_marker)
            self.assertGreaterEqual(marker_pos, 0, failure_marker)
            preceding = DASHBOARD[max(0, marker_pos - 180):marker_pos]
            self.assertIn("dashResumeTransmitAfterOtaFailure();", preceding, failure_marker)


if __name__ == "__main__":
    unittest.main()