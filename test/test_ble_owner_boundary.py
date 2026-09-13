"""Contract tests for the fail-closed BLE owner-authorization boundary."""
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BLE = ROOT / "include/ble/ble_service.h"


class BleOwnerBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = BLE.read_text(encoding="utf-8")

    def test_raw_can_driver_send_path_is_absent(self) -> None:
        self.assertIn('strcmp(cmd, "send")', self.source)
        self.assertIn("raw BLE CAN send removed; TxIntent required", self.source)
        self.assertNotIn("dashDriver->send", self.source)
        self.assertNotIn("kBleSendMaxFrames", self.source)
        self.assertNotIn("bleParseHexPayload", self.source)

    def test_state_changes_fail_closed_without_owner_authorization(self) -> None:
        self.assertIn("device-owner authorization required", self.source)
        self.assertIn("device-owner CAN-arm permission required", self.source)
        self.assertIn("device-owner admin permission required", self.source)
        self.assertNotIn("ctrlApplyConfig(source)", self.source)
        self.assertNotIn('dashSetCanActive(on.as<bool>(), "ble")', self.source)
        self.assertNotIn("dashSetBleMode(false)", self.source)

    def test_read_only_commands_remain_available(self) -> None:
        for command in ("status", "ping", "stats", "snapshot", "config"):
            self.assertIn(f'strcmp(cmd, "{command}")', self.source)
        self.assertIn("ctrlBuildConfigJson()", self.source)
        self.assertIn("dashBuildBleMaintenanceSnapshotJson()", self.source)

    def test_owner_lifecycle_is_wired_without_enabling_can(self) -> None:
        for command in (
            "owner_status",
            "owner_enroll",
            "owner_permissions",
            "owner_revoke",
            "owner_replace_begin",
        ):
            self.assertIn(f'strcmp(cmd, "{command}")', self.source)
        self.assertIn('storage.putString("record", expected)', self.source)
        self.assertIn('verify.getString("record", "")', self.source)
        self.assertIn("actual == expected", self.source)
        self.assertNotIn("privateKey", self.source)
        self.assertNotIn("ownerSecret", self.source)


if __name__ == "__main__":
    unittest.main()
