import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BLE = ROOT / "include/ble/ble_service.h"
DASHBOARD = ROOT / "include/web/mcp2515_dashboard.h"


class BleMaintenanceSnapshotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ble = BLE.read_text(encoding="utf-8")
        cls.dashboard = DASHBOARD.read_text(encoding="utf-8")

    def test_snapshot_command_is_read_only_and_versioned(self):
        self.assertIn('strcmp(cmd, "snapshot")', self.ble)
        self.assertIn("dashBuildBleMaintenanceSnapshotJson()", self.ble)
        self.assertIn('\\"t2can-maintenance-snapshot-v1\\"', self.dashboard)
        self.assertIn('\\"readOnly\\":true', self.dashboard)
        self.assertIn('\\"driver\\":', self.dashboard)
        self.assertIn('\\"telemetry\\":', self.dashboard)
        self.assertIn('\\"config\\":', self.dashboard)

    def test_snapshot_has_no_can_send_or_ota_path(self):
        start = self.dashboard.index("static String dashBuildBleMaintenanceSnapshotJson()")
        end = self.dashboard.index("// Compact status for the BLE app", start)
        snapshot = self.dashboard[start:end]
        self.assertNotIn("send(", snapshot)
        self.assertNotIn("Update.", snapshot)
        self.assertIn("dashTelemetry.snapshot", snapshot)
        self.assertIn("dashDriver->diagnosticsJson", snapshot)


if __name__ == "__main__":
    unittest.main()
