import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
TWAI = (ROOT / "include/drivers/twai_driver.h").read_text()
DUAL = (ROOT / "include/drivers/dual_can_driver.h").read_text()


class PerBusHealthContractTest(unittest.TestCase):
    def test_twai_bus_off_publishes_fault_epoch(self):
        self.assertIn("uint32_t faultEpoch() const", TWAI)
        self.assertIn("status.state == TWAI_STATE_BUS_OFF", TWAI)
        self.assertIn("++faultEpoch_;", TWAI)
        self.assertIn("busFaultLatched_", TWAI)

    def test_dual_driver_consumes_fault_epoch_after_can_b_send(self):
        self.assertIn("canBFaultEpoch_ = canB_.faultEpoch();", DUAL)
        self.assertIn("okB = canBHealth_.txAllowed(now) && canB_.sendWithAttempt", DUAL)
        self.assertIn("syncCanBFault();\n            if (attemptedB)", DUAL)
        self.assertIn("canBHealth_.noteControllerFault();", DUAL)


if __name__ == "__main__":
    unittest.main()
