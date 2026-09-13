"""Contracts for the compact firmware manifest and board-artifact gate."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
DASHBOARD = (ROOT / "include/web/mcp2515_dashboard.h").read_text(encoding="utf-8")
SYNC = (ROOT / "scripts/platformio_sync_profile.py").read_text(encoding="utf-8")


class FirmwareManifestContractTest(unittest.TestCase):
    def test_build_injects_revision_and_environment(self) -> None:
        self.assertIn('("FIRMWARE_GIT_REV",', SYNC)
        self.assertIn('("FIRMWARE_BUILD_ENV",', SYNC)
        self.assertIn('["git", "rev-parse", "--short=12", "HEAD"]', SYNC)

    def test_status_manifest_has_required_identity_and_policy_fields(self) -> None:
        for token in (
            't2can-firmware-manifest-v1',
            'firmwareVersion',
            'gitRevision',
            'buildEnvironment',
            'physicalBuses',
            'semanticBuses',
            'features',
            'decoderSchema',
            'txPolicy',
            'effectiveMode',
            'configDigest',
            'selfTest',
        ):
            with self.subTest(token=token):
                self.assertIn(token, DASHBOARD)

    def test_config_digest_excludes_secret_fields(self) -> None:
        start = DASHBOARD.index("const uint8_t manifestConfig[]")
        end = DASHBOARD.index("uint32_t manifestDigest", start)
        digest_inputs = DASHBOARD[start:end]
        for secret in ("DASH_PASS", "DASH_OTA_PASS", "staPass", "apPass"):
            with self.subTest(secret=secret):
                self.assertNotIn(secret, digest_inputs)

    def test_manual_ota_rejects_wrong_artifact_before_quiesce_or_flash(self) -> None:
        start = DASHBOARD.index("if (upload.status == UPLOAD_FILE_START)")
        end = DASHBOARD.index("else if (upload.status == UPLOAD_FILE_WRITE)", start)
        block = DASHBOARD[start:end]
        reject = block.index("upload.filename != FIRMWARE_ARTIFACT")
        quiesce = block.index('dashQuiesceTransmitForOta("web_upload")')
        flash = block.index("Update.begin(upload.totalSize)")
        self.assertLess(reject, quiesce)
        self.assertLess(reject, flash)


if __name__ == "__main__":
    unittest.main()
