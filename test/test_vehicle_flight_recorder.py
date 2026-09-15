import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class VehicleFlightRecorderWiringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.recorder = (ROOT / "include/chassis/event_recorder.h").read_text(encoding="utf-8")
        cls.runtime = (ROOT / "include/runtime_diagnostics.h").read_text(encoding="utf-8")
        cls.runtime_source = (ROOT / "src/espidf_runtime.cpp").read_text(encoding="utf-8")
        cls.app = (ROOT / "include/app.h").read_text(encoding="utf-8")
        cls.dashboard = (ROOT / "include/web/mcp2515_dashboard.h").read_text(encoding="utf-8")
        cls.plugin = (ROOT / "include/plugin_engine.h").read_text(encoding="utf-8")
        cls.drivers = "".join(
            (ROOT / path).read_text(encoding="utf-8")
            for path in (
                "include/drivers/can_driver.h",
                "include/drivers/dual_can_driver.h",
                "include/drivers/esp32_mcp2515_driver.h",
                "include/drivers/twai_driver.h",
            )
        )
        cls.doc = (ROOT / "docs/vehicle-flight-recorder.md").read_text(encoding="utf-8")

    def test_configuration_and_profile_transitions_share_lock_order(self):
        self.assertIn("AppHandlerGuard appGuard;\n        DashDataGuard guard;", self.dashboard)
        self.assertIn("AppHandlerGuard appGuard;\n#endif\n    PluginLockGuard guard(false);", self.plugin)
        self.assertIn("PluginLockGuard pluginGuard;\n        DashDataGuard dataGuard;", self.dashboard)
        self.assertIn("const uint32_t commitNow = millis();", self.dashboard)
        self.assertIn("dashRecordEffectiveConfigurationAt(commitNow, pluginGetReplayCountLocked());", self.dashboard)
        self.assertIn("handlerPool[i]->onSpeedProfileChanged = mcpDashOnActiveSpeedProfile;", self.dashboard)
        self.assertIn("speedProfileChangeMs", self.dashboard + (ROOT / "include/handlers.h").read_text(encoding="utf-8"))
        self.assertIn("notifySpeedProfileChanged(previousProfile, profileChangeMs);", self.app + self.dashboard + (ROOT / "include/handlers.h").read_text(encoding="utf-8"))

    def test_recording_contract_is_observational_and_bounded(self):
        for token in ("observeTx", "Direction::Rx", "Direction::Tx", "recordInjectionDecision",
                      "rawDrops_", "stateDrops_", "PostWindowMs = 10000", "usingPsram"):
            self.assertIn(token, self.recorder)
        self.assertIn("appDashboardDecisionObserver", self.app)
        self.assertIn("mcpDashOnInjectionDecision", self.dashboard)

    def test_boot_probe_and_board_local_atomic_persistence_are_wired(self):
        for token in ("probePsram", "MALLOC_CAP_SPIRAM", "psramVerified", "psramProbeBytes"):
            self.assertIn(token, self.runtime)
        for token in ("/incident.tmp", "/incident.jsonl", "SPIFFS.rename", "t2can-flight-recorder-v1",
                      "dashPersistFrozenIncident", "dashPublicationFromName", "CanLoss"):
            self.assertIn(token, self.dashboard)
        self.assertIn("SPIFFS.begin(false)", self.dashboard)
        self.assertIn("automatic upload", self.doc)
        self.assertIn("10,000 ms", self.doc)

    def test_routes_and_status_expose_measured_storage(self):
        for token in ('server.on("/event_control"', 'server.on("/event_list"',
                      'server.on("/event_download"', 'server.on("/event_ack"',
                      'rawCapacity', 'stateCapacity', 'coverageMs', 'stateCoverageMs',
                      'stateTargetReady', 'incidentStore', 'persistFailure'):
            self.assertIn(token, self.dashboard)

    def test_successive_incidents_and_rearm_are_guarded(self):
        for token in ("dashChooseIncidentPath", "dashFindLatestIncidentPath", "DashPersistLease",
                      "dashPersistInProgress", "server.send(409", "effectiveSettingCount",
                      "EffectiveSetting", "frozenEffectiveSettings_", 'getString("inc_seq"',
                      'putString("inc_seq"', "dashEnsureIncidentSpace", "storage_full_unacknowledged",
                      "dashHashIncident", "dashWriteAck", "dashRequireRecorderAuth"):
            self.assertIn(token, self.dashboard + self.recorder)

    def test_state_history_has_a_real_five_minute_policy(self):
        for token in ("StateTargetWindowMs", "StateHistoryBucketMs",
                      "InternalStateHistoryBudgetPerBucket", "PsramStateHistoryBudgetPerBucket",
                      "stateHistoryCoverageMs", "stateTargetWindowReady", "stateProtectedDrops_"):
            self.assertIn(token, self.recorder)
        self.assertIn("refuses to evict a state record younger than 300,000 ms", self.doc)

    def test_incident_export_contract_is_loss_aware(self):
        for token in ("GET /event_list", "GET /event_download?id=", "POST /event_ack",
                      "never automatically deletes an unacknowledged incident", "SHA-256",
                      "reserved in NVS"):
            self.assertIn(token, self.doc)

    def test_health_and_upstream_gate_outcomes_are_recorded(self):
        for token in ("recordCanHealth", "recordCanLiveness", "physicalHealth", "healthErrorCount",
                      "physicalBus", "onSendAttempt", "CanHealth", "CanLiveness", "can_disabled",
                      "ap_gate_blocked", "nag_disabled", "can_anomaly", "activity_gate_blocked",
                      "maintenance", "return saved", "sendWithAttempt",
                      "reportAttempt(frame, CAN_BUS_CAN_A", "reportAttempt(frame, CAN_BUS_CAN_B",
                      "frame.physicalBus = physicalBus()", "physicalBus\\\":%u"):
            self.assertIn(token, self.dashboard + self.recorder + self.app + self.drivers)

    def test_decision_reason_codes_cover_final_tx_gates(self):
        for mapping in (
            'strcmp(reason, "maintenance") == 0) code = 7',
            'strcmp(reason, "can_anomaly") == 0) code = 8',
            'strcmp(reason, "activity_gate_blocked") == 0) code = 9',
        ):
            self.assertIn(mapping, self.dashboard)

    def test_deadline_maintenance_runtime_config_and_download_errors_are_wired(self):
        for token in ("postDeadlineReached", "dashStartRecorderMaintenance", "recorderMaintenanceTask",
                      "dashRecordEffectiveConfiguration();", "size_t expectedBytes = 0;",
                      "size_t sentBytes = 0", "const bool streamOk = server.streamFile(saved",
                      "Incident download failed", "hasReadError", "responseFailed_",
                      "return responseFailed_ ? ESP_FAIL : ESP_OK", "httpd_resp_send_chunk(currentReq_, nullptr, 0)",
                      "configurationUpdateDepth_", "configurationUpdateActive()", "effectiveSpeedProfile",
                      "dashHandler->speedProfile"):
            self.assertIn(token, self.dashboard + self.recorder + self.runtime_source)


if __name__ == "__main__":
    unittest.main()
