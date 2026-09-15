import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class LilygoT2CanBuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
        cls.tests_workflow = (ROOT / ".github/workflows/tests.yml").read_text(
            encoding="utf-8"
        )
        cls.release_workflow = (ROOT / ".github/workflows/release.yml").read_text(
            encoding="utf-8"
        )
        cls.main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        cls.sync = (ROOT / "scripts/platformio_sync_profile.py").read_text(
            encoding="utf-8"
        )
        cls.psram_defaults = (
            ROOT / "sdkconfig.defaults.lilygo_t2can"
        ).read_text(encoding="utf-8")

    def test_board_environment_uses_both_physical_can_buses(self) -> None:
        environment = self.platformio.split("[env:lilygo_t2can]", 1)[1].split(
            "[env:", 1
        )[0]
        self.assertIn("-DDRIVER_T2CAN_DUAL", environment)
        self.assertIn("-DPIN_CAN_CS=10", environment)
        self.assertIn("-DPIN_CAN_INTERRUPT=8", environment)
        self.assertIn("-DPIN_CAN_RESET=9", environment)
        self.assertIn("-DSPI_SCK=12", environment)
        self.assertIn("-DSPI_MISO=13", environment)
        self.assertIn("-DSPI_MOSI=11", environment)
        self.assertIn("-DMCP_CRYSTAL_FREQ=MCP_16MHZ", environment)
        self.assertIn("-DTWAI_TX_PIN=GPIO_NUM_7", environment)
        self.assertIn("-DTWAI_RX_PIN=GPIO_NUM_6", environment)
        self.assertIn("-DPIN_LED=2", environment)
        self.assertIn("-DDASH_RGB_STATUS_LED", environment)
        self.assertIn("partitions_16mb_ota_4096k_nvs64.csv", environment)
        self.assertIn("sdkconfig.defaults.lilygo_t2can", environment)

    def test_board_enables_optional_opi_psram_with_safe_fallback(self) -> None:
        self.assertIn("CONFIG_SPIRAM=y", self.psram_defaults)
        self.assertIn("CONFIG_SPIRAM_MODE_OCT=y", self.psram_defaults)
        self.assertIn("CONFIG_SPIRAM_IGNORE_NOTFOUND=y", self.psram_defaults)
        self.assertIn("CONFIG_SPIRAM_USE_CAPS_ALLOC=y", self.psram_defaults)
        self.assertIn("_sync_t2can_psram", self.sync)

    def test_board_hardware_resets_mcp2515(self) -> None:
        self.assertIn("#ifdef PIN_CAN_RESET", self.main)
        self.assertIn("digitalWrite(PIN_CAN_RESET, LOW)", self.main)

    def test_automatic_builds_include_board(self) -> None:
        self.assertIn("- env: lilygo_t2can", self.tests_workflow)
        self.assertIn("- env: lilygo_t2can", self.release_workflow)
        self.assertIn(
            "- env: lilygo_t2can\n            profile: --driver DRIVER_T2CAN_DUAL",
            self.tests_workflow,
        )
        self.assertIn(
            "- env: lilygo_t2can\n            profile: --driver DRIVER_T2CAN_DUAL",
            self.release_workflow,
        )

    def test_release_artifact_is_board_specific(self) -> None:
        mapping = '"lilygo_t2can": "firmware-lilygo-t2can.bin"'
        self.assertIn(mapping, self.sync)
        self.assertIn("firmware-lilygo-t2can.bin", self.release_workflow)


if __name__ == "__main__":
    unittest.main()
