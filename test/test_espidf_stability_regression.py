import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class EspIdfStabilityRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        cls.app = (ROOT / "include/app.h").read_text(encoding="utf-8")
        cls.runtime = (ROOT / "include/runtime_diagnostics.h").read_text(encoding="utf-8")
        cls.twai = (ROOT / "include/drivers/twai_driver.h").read_text(encoding="utf-8")
        cls.external_mcp = (ROOT / "include/drivers/esp32_mcp2515_driver.h").read_text(encoding="utf-8")
        cls.handlers = (ROOT / "include/handlers.h").read_text(encoding="utf-8")
        cls.injection_policy = (ROOT / "include/injection_policy.h").read_text(encoding="utf-8")
        cls.plugin_engine = (ROOT / "include/plugin_engine.h").read_text(encoding="utf-8")
        cls.dashboard = (ROOT / "include/web/mcp2515_dashboard.h").read_text(encoding="utf-8")
        cls.ui = (ROOT / "include/web/mcp2515_dashboard_ui.src.h").read_text(encoding="utf-8")
        cls.espidf_runtime = (ROOT / "src/espidf_runtime.cpp").read_text(encoding="utf-8")
        cls.gvret = (ROOT / "include/gvret_serial.h").read_text(encoding="utf-8")
        cls.optimization_doc = (ROOT / "docs/esp32-optimization.md").read_text(encoding="utf-8")

    def test_dashboard_starts_before_twai_and_wake_delay(self) -> None:
        twai_branch = self.main[self.main.index("#elif defined(DRIVER_TWAI)") :]
        self.assertLess(twai_branch.index("mcpDashboardSetup"), twai_branch.index("delay(DRIVER_WAKE_DELAY_MS)"))
        self.assertLess(twai_branch.index("delay(DRIVER_WAKE_DELAY_MS)"), twai_branch.index("appStartDriver<TWAIDriver>"))

    def test_injection_uses_actual_can_time_and_live_frames(self) -> None:
        self.assertIn("DRIVER_WAKE_DELAY_MS = 10000", self.runtime)
        self.assertIn("INJECTION_DELAY_MS = 15000", self.runtime)
        self.assertIn("CAN_LIVE_FRAME_THRESHOLD = 1000", self.runtime)
        self.assertIn("now - initialized >= INJECTION_DELAY_MS", self.runtime)
        self.assertIn("canFrames.load(std::memory_order_relaxed) > CAN_LIVE_FRAME_THRESHOLD", self.runtime)
        self.assertIn("appDriver->allowSendFrame = appCanTransmitAllowed", self.app)

    def test_summon_only_policy_is_central_and_clears_pending_transmit(self) -> None:
        self.assertIn("evaluateSummonInjectionPolicy", self.injection_policy)
        self.assertIn("kSummonInjectionFreshnessMs = 1000", self.injection_policy)
        self.assertIn("summonOnlyInjectionDecisionAt", self.app)
        self.assertIn("sendAllowed(frame)", self.twai)
        self.assertIn("sendAllowed(frame)", self.external_mcp)
        self.assertIn("twai_clear_transmit_queue()", self.twai)
        self.assertIn("void clearPendingTransmit() override", self.external_mcp)
        self.assertIn("mcp_.abortPendingTransmissions()", self.external_mcp)
        self.assertIn("pluginResetPeriodicEmit()", self.dashboard)
        self.assertIn("dashRefreshSummonOnlyPolicy();", self.app)
        self.assertIn("driver.send(pluginPeriodicEmit.frame)", self.plugin_engine)

    def test_twai_recovery_and_state_contract(self) -> None:
        self.assertIn("twai_initiate_recovery()", self.twai)
        self.assertIn("twai_transmit(&msg, pdMS_TO_TICKS(2))", self.twai)
        self.assertIn("now - lastTxFailLogMs_ >= 2000", self.twai)
        for state in ("TWAI_STATE_STOPPED", "TWAI_STATE_RUNNING", "TWAI_STATE_BUS_OFF", "TWAI_STATE_RECOVERING"):
            self.assertIn(state, self.twai)

    def test_nag_hard_limits_and_full_torque_compare(self) -> None:
        self.assertIn("TORQUE_NM_MAX = +1.80f", self.handlers)
        self.assertIn("TORQUE_NM_MIN = -1.80f", self.handlers)
        self.assertIn("TORQUE_RAW_MAX = 0x08B6", self.handlers)
        self.assertIn("TORQUE_RAW_MIN = 0x074E", self.handlers)
        self.assertIn("torqueRaw == TORQUE_RAW_MAX", self.handlers)
        self.assertIn("frame.dlc < 8", self.handlers)

    def test_dashboard_nag_modes_are_built_in_and_fail_closed(self) -> None:
        for mode in ("ModeA", "ModeB", "ModeC"):
            self.assertIn(mode, self.handlers)
        self.assertIn("kContextFreshMs = 1000", self.handlers)
        self.assertIn("nagModeAllowedForHardware", self.handlers)
        self.assertIn("Nag Mode C is blocked on HW4", self.dashboard)
        self.assertIn("dashApGateSnapshot", self.dashboard)
        self.assertIn("AP gate state", self.dashboard)
        self.assertIn("readDASAutopilotHandsOnState", self.handlers)
        self.assertIn("readSCCMSteeringAngleValidity", self.handlers)
        self.assertIn("dashNagProcess(original, *appDriver)", self.app)
        self.assertIn('prefs.putUChar("nag_mode", dashNagMode)', self.dashboard)
        # The config arguments are read inside ctrlApplyConfig now, which both
        # POST /config and the BLE config command run -- so also assert the HTTP
        # route still reaches it, or this guard would pass on a BLE-only path.
        self.assertIn('args.has("nag")', self.dashboard)
        self.assertIn("ConfigResult ctrlApplyConfig(const ConfigArgs &args)", self.dashboard)
        self.assertIn("ctrlApplyConfig(args)", self.dashboard)
        self.assertIn("struct ServerArgs : ConfigArgs", self.dashboard)
        self.assertIn('id="nag-off"', self.ui)
        self.assertIn('id="nag-a"', self.ui)
        self.assertIn('id="nag-b"', self.ui)
        self.assertIn('id="nag-c"', self.ui)
        self.assertIn("플러그인을 사용하지 않습니다", self.ui)

    def test_juniper_hw4_catalog_uses_existing_plugins_and_installs_disabled(self) -> None:
        for plugin_name in (
            "ISA Chime Suppress HW4",
            "FSD Activation HW4 (without TLSSC bypass)",
            "Bypass TLSSC HW4 and include FSD activation",
            "Emergency Vehicle Detection HW4 with FSD enabling",
        ):
            self.assertIn(plugin_name, self.ui)
        self.assertNotIn("Summon EU Unlock for all cars", self.ui)
        self.assertIn("[[5,7],[7,10],[10,14],[15,21]]", self.ui)
        self.assertIn("`HW4 Speed Offset +${offset}`", self.ui)
        self.assertIn("T2CAN Sentinel", self.ui)
        self.assertIn('GITHUB_REPO = "hongho55/ev-open-can-tools"', self.dashboard)
        self.assertNotIn('GITHUB_REPO = "ev-open-can-tools/ev-open-can-tools"', self.dashboard)
        self.assertIn('String(GITHUB_REPO) + "/releases/download/"', self.dashboard)
        self.assertIn("kSentinelReleaseChannelPublished = true", self.dashboard)
        self.assertIn('releaseDigest = String(asset["digest"] | "")', self.dashboard)
        self.assertIn("dashInstallVerifiedFirmware", self.dashboard)
        self.assertIn("fresh Park gear state required", self.dashboard)
        self.assertIn("fresh zero-speed state required", self.dashboard)
        self.assertIn("fresh vehicle OTA state required", self.dashboard)
        self.assertIn("!telemetry.vehicleOtaFresh", self.dashboard)
        self.assertIn("Sentinel 릴리스 채널", self.ui)
        self.assertIn("pendingUpdateDigest", self.ui)
        self.assertIn("https://github.com/hongho55/ev-open-can-tools/issues/new", self.ui)
        self.assertNotIn("https://github.com/ev-open-can-tools/ev-open-can-tools/issues/new", self.ui)
        self.assertIn("t2can-sentinel-settings.json", self.ui)
        self.assertIn('Basic realm=\\"T2CAN Sentinel\\"', self.espidf_runtime)
        self.assertIn("requested && !kSentinelReleaseChannelPublished", self.dashboard)
        self.assertIn("installCatalogPlugin", self.ui)
        self.assertIn("temp.enabled = false", self.dashboard)

    def test_only_last_write_check_remains_from_removed_diagnostics(self) -> None:
        self.assertIn("마지막 송신 확인", self.ui)
        for removed in ("CAN Sniffer", "CAN Recorder", "CAN Controller", "Live Log", "Rule Test", "Plugin Editor"):
            self.assertNotIn(removed, self.ui)
        for route in ('"/frames"', '"/log"', '"/rec_start"', '"/plugin_test"', '"/reset_stats"'):
            self.assertNotIn(route, self.dashboard)

    def test_gvret_is_bounded_read_only_and_nonblocking(self) -> None:
        self.assertIn("kMaxRunMs = 10UL * 60UL * 1000UL", self.gvret)
        self.assertIn("kClientIdleMs = 15000", self.gvret)
        self.assertIn("active ? 10 : 250", self.gvret)
        self.assertNotIn("twai_transmit", self.gvret)
        self.assertNotIn("sendMessage", self.gvret)

    def test_support_is_in_diagnostics_on_demand_and_single_endpoint(self) -> None:
        support_position = self.ui.index('id="support-card"')
        self.assertGreater(support_position, self.ui.index('id="panel-diagnostics"'))
        self.assertLess(support_position, self.ui.index('id="panel-updates"'))
        self.assertLess(support_position, self.ui.index('class="footer"'))
        self.assertIn('<details class="card" id="support-card"', self.ui)
        self.assertIn("requestText('/support')", self.ui)
        self.assertIn("locked('support'", self.ui)
        self.assertIn('server.on("/support", HTTP_GET, handleSupport)', self.dashboard)
        self.assertNotIn("setInterval(loadSupport", self.ui)

    def test_support_report_has_required_sections_without_secrets(self) -> None:
        support = self.dashboard[
            self.dashboard.index("static void handleSupport()") :
            self.dashboard.index("static void handleConfigGet()")
        ]
        for section in (
            "[Firmware]",
            "[Memory]",
            "[Tasks]",
            "[WiFi]",
            "[Configuration]",
            "[CAN]",
            "[Last Write Check]",
            "[Safety]",
            "[NVS]",
            "[USB Serial / SavvyCAN]",
            "[Web]",
        ):
            self.assertIn(section, support)
        for secret in ("apPass", "staPass", "DASH_OTA_PASS", "DASH_OTA_USER", "wifiNetworks", "private", "token"):
            self.assertNotIn(secret, support)

    def test_runtime_optimizations_are_bounded_and_reduce_idle_polling(self) -> None:
        status = self.dashboard[
            self.dashboard.index("static void handleStatus()") :
            self.dashboard.index("static const char *dashWriteProbeStateName")
        ]
        response_match = re.search(r"\bchar response\[(\d+)\];", status)
        self.assertIsNotNone(response_match)
        response_size = int(response_match.group(1)) if response_match else 0
        self.assertGreaterEqual(response_size, 4096)
        self.assertIn("BoundedTextWriter json(response, sizeof(response));", status)
        self.assertIn("BoundedTextWriter", status)
        self.assertNotIn("String j", status)
        self.assertNotIn("setInterval(loadPlugins", self.ui)
        self.assertIn("setInterval(pollRuntime,3000)", self.ui)
        self.assertIn("setInterval(loadWifi,30000)", self.ui)
        self.assertIn("pdMS_TO_TICKS(250)", self.dashboard)
        self.assertIn("active ? 10 : 250", self.gvret)

    def test_optimization_document_covers_hardware_gated_tuning(self) -> None:
        for topic in (
            "Task stacks",
            "Heap fragmentation",
            "Task priorities",
            "Dashboard polling",
            "NVS wear",
            "CAN queue pressure",
            "ESP-IDF heap tools",
            "Compiler optimization",
            "Firmware/partition size",
            "CPU profiling",
        ):
            self.assertIn(topic, self.optimization_doc)


if __name__ == "__main__":
    unittest.main()
