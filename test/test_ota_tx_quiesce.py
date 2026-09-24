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
        self.assertRegex(
            DASHBOARD,
            r'dashQuiesceTransmitForOta\("web_upload"\);\s*'
            r"manualOtaAccepted = Update\.begin\(",
        )
        helper = DASHBOARD[
            DASHBOARD.index("static bool dashInstallVerifiedFirmware") :
            DASHBOARD.index("static bool dashAutomaticOtaAllowed")
        ]
        self.assertIn("Update.begin(contentLength)", helper)
        for function_name, source in (
            ("handleUpdateInstall", "web_install"),
            ("performAutoUpdate", "auto_update"),
        ):
            start = DASHBOARD.index(f"static void {function_name}()\n{{")
            end = DASHBOARD.index("\n}\n", start)
            code = DASHBOARD[start:end]
            quiesce = code.index(f'dashQuiesceTransmitForOta("{source}");')
            install = code.index("dashInstallVerifiedFirmware")
            self.assertLess(quiesce, install, source)
            self.assertIn("DashMaintenanceCleanupGuard cleanup;", code[quiesce:install])

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
        ):
            marker_pos = DASHBOARD.find(failure_marker)
            self.assertGreaterEqual(marker_pos, 0, failure_marker)
            preceding = DASHBOARD[max(0, marker_pos - 180):marker_pos]
            self.assertIn("dashResumeTransmitAfterOtaFailure();", preceding, failure_marker)
        for function_name, failure_marker in (
            ("handleUpdateInstall", '[OTA] Verified install failed:'),
            ("performAutoUpdate", '[AUTO-OTA] Verified install failed:'),
        ):
            start = DASHBOARD.index(f"static void {function_name}()\n{{")
            end = DASHBOARD.index("\n}\n", start)
            code = DASHBOARD[start:end]
            self.assertIn("DashMaintenanceCleanupGuard cleanup;", code)
            self.assertIn(failure_marker, code)
            self.assertLess(code.index("DashMaintenanceCleanupGuard cleanup;"), code.index(failure_marker))


if __name__ == "__main__":
    unittest.main()