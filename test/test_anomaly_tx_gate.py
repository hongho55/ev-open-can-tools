"""Contract checks for anomaly-to-final-TX-gate wiring."""
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APP = (ROOT / "include/app.h").read_text(encoding="utf-8")
DASH = (ROOT / "include/web/mcp2515_dashboard.h").read_text(encoding="utf-8")


class AnomalyTxGateTests(unittest.TestCase):
    def test_final_driver_gate_checks_anomaly_before_allow(self) -> None:
        self.assertIn("appDashboardAnomalyBlocksTx", APP)
        check = APP.index("appDashboardAnomalyBlocksTx && appDashboardAnomalyBlocksTx()")
        allow = APP.index('appDashboardDecisionObserver(true, "allowed")')
        self.assertLess(check, allow)
        self.assertIn('appDashboardDecisionObserver(false, "can_anomaly")', APP)

    def test_dashboard_wires_sticky_policy_block(self) -> None:
        self.assertIn("appDashboardAnomalyBlocksTx = dashAnomalyBlocksTx", DASH)
        self.assertIn("return dashAnomalyTracker.summary().policyBlock", DASH)
        self.assertIn("dashAnomalyTracker.tick(millis())", DASH)

    def test_anomaly_gate_does_not_send(self) -> None:
        start = DASH.index("static bool dashAnomalyBlocksTx()")
        end = DASH.index("class DashRecorderConfigUpdate", start)
        body = DASH[start:end]
        self.assertNotIn("send(", body)
        self.assertNotIn("dashDriver", body)


if __name__ == "__main__":
    unittest.main()
