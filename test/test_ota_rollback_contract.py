import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class OtaRollbackContractTests(unittest.TestCase):
    def test_t2can_bootloader_rollback_is_enabled(self):
        sdkconfig = (ROOT / "sdkconfig.defaults.lilygo_t2can").read_text()
        self.assertIn("CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y", sdkconfig)

    def test_pending_image_inhibits_tx_before_app_setup(self):
        source = (ROOT / "src/main.cpp").read_text()
        begin = source.index("appBeginOtaBootVerification();")
        setup = source.index("app_main_setup();", begin)
        self.assertLess(begin, setup)
        self.assertIn("appMaintenanceTxInhibit = true;", source)
        self.assertIn("ESP_OTA_IMG_PENDING_VERIFY", source)

    def test_confirmation_requires_local_preflight(self):
        source = (ROOT / "src/main.cpp").read_text()
        self.assertIn("const bool preflightPassed = nvsOk && psramOk && driverOk && selfTestOk;", source)
        self.assertIn("requiredPassedResults = 2", source)
        self.assertIn("esp_ota_mark_app_valid_cancel_rollback()", source)
        self.assertIn("esp_ota_mark_app_invalid_rollback_and_reboot()", source)
        self.assertIn('strstr(selfTest, "\\\"physicalTx\\\":true") == nullptr', source)

    def test_nvs_failure_enters_explicit_rollback_before_abort(self):
        source = (ROOT / "src/main.cpp").read_text()
        note = source.index("RuntimeDiagnostics::noteNvsInitialization")
        rollback = source.index("appCompleteOtaBootVerification();", note)
        abort = source.index("ESP_ERROR_CHECK(nvsErr);", note)
        self.assertLess(rollback, abort)
        self.assertIn("nvsErr != ESP_OK && appOtaBootGuard.pending()", source[note:abort])

    def test_dashboard_cannot_release_pending_or_rollback_inhibit(self):
        dashboard = (ROOT / "include/web/mcp2515_dashboard.h").read_text()
        body = dashboard.split("static void dashResumeTransmitAfterOtaFailure()", 2)[2].split("\n}", 1)[0]
        self.assertIn("OtaBootState::Pending", body)
        self.assertIn("OtaBootState::Rollback", body)
        self.assertLess(body.index("OtaBootState::Pending"), body.index("appMaintenanceTxInhibit = false;"))

    def test_status_exposes_boot_transaction(self):
        dashboard = (ROOT / "include/web/mcp2515_dashboard.h").read_text()
        self.assertIn('\\"otaBoot\\"', dashboard)
        self.assertIn("RuntimeDiagnostics::otaBootStateName()", dashboard)
        self.assertIn("otaPreflightPassed", dashboard)


if __name__ == "__main__":
    unittest.main()
