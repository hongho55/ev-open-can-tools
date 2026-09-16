import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APP = (ROOT / "include/app.h").read_text(encoding="utf-8")
DASHBOARD = (ROOT / "include/web/mcp2515_dashboard.h").read_text(encoding="utf-8")


class MasterTxGateTests(unittest.TestCase):
    def test_driver_final_gate_checks_dashboard_master_enable(self):
        body = re.search(
            r"static bool appCanTransmitAllowed\(const CanFrame &\)\s*\{(.*?)\n\}",
            APP,
            re.S,
        )
        if body is None:
            self.fail("appCanTransmitAllowed definition missing")
        code = body.group(1)
        self.assertIn("!appDashboardMasterTxEnabled", code)
        self.assertIn("!appDashboardMasterTxEnabled()", code)
        self.assertIn('"can_disabled"', code)

    def test_dashboard_gate_reads_live_persisted_choice(self):
        self.assertIn(
            "appDashboardMasterTxEnabled = []() { return static_cast<bool>(canActive); };",
            DASHBOARD,
        )
        self.assertIn('prefs.putBool("can", canActive);', DASHBOARD)
        self.assertIn('canActive = prefs.getBool("can", kDashInjectionDefaultEnabled);', DASHBOARD)

    def test_dashboard_tx_context_fails_closed_on_autopark_state(self):
        self.assertIn(
            "context.autoparkBlocked = !telemetry.diStateSeen || telemetry.autoparkActive;",
            DASHBOARD,
        )
        body = re.search(
            r"static bool dashActivityTxAllowed\(\)\s*\{(.*?)\n\}",
            DASHBOARD,
            re.S,
        )
        if body is None:
            self.fail("dashActivityTxAllowed definition missing")
        code = body.group(1)
        fail_closed = "if (!telemetry.diStateSeen || telemetry.autoparkActive)"
        reset_stability = "dashTrackApStableMs(false, now);"
        optional_gate = "if (!apInjectionGate)"
        self.assertIn(fail_closed, code)
        self.assertIn(reset_stability, code)
        self.assertIn(optional_gate, code)
        self.assertLess(code.index(fail_closed), code.index(optional_gate))
        self.assertLess(code.index(reset_stability), code.index(optional_gate))


if __name__ == "__main__":
    unittest.main()
