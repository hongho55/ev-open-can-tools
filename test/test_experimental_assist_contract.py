#!/usr/bin/env python3
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
DASH = (ROOT / "include/web/mcp2515_dashboard.h").read_text()
UI = (ROOT / "include/web/mcp2515_dashboard_ui.src.h").read_text()
POLICY = (ROOT / "include/tx/tx_intent.h").read_text()
HANDLERS = (ROOT / "include/handlers.h").read_text()
APP = (ROOT / "include/app.h").read_text()


class ExperimentalAssistContract(unittest.TestCase):
    def test_defaults_are_fail_closed(self):
        for declaration in (
            "static Shared<bool> dashUlcStalkConfirm{false};",
            "static Shared<bool> dashUlcOffHighway{false};",
            "static Shared<bool> dashSummonEuUnlock{false};",
            "static Shared<bool> dashHandsOn247DryRunEnabled{false};",
        ):
            self.assertIn(declaration, DASH)
        self.assertIn("dashUlcSpeedConfig{ExperimentalAssist::kPreserveSetting}", DASH)
        self.assertIn("dashUlcBlindSpotConfig{ExperimentalAssist::kPreserveSetting}", DASH)

    def test_config_api_and_ui_are_wired(self):
        for name in ("ulcStalk", "ulcOffHighway", "ulcSpeed", "ulcBlind", "summonEu", "hands247"):
            self.assertIn(f'args.has("{name}")', DASH)
            self.assertIn(f"{name}:", UI)
        for element_id in (
            "ulc-stalk-tgl",
            "ulc-offhwy-tgl",
            "ulc-speed",
            "ulc-blind",
            "summon-eu-tgl",
            "hands247-dry-tgl",
            "hands247-stats",
        ):
            self.assertIn(f'id="{element_id}"', UI)
        self.assertIn("AP 상태 게이트 (OFF=상시주입)", UI)
        self.assertIn('id="s-inj"', UI)

    def test_research_ids_remain_non_transmittable(self):
        self.assertIn("intent.expectedId == 0x229", POLICY)
        self.assertIn("intent.expectedId == 0x247", POLICY)
        self.assertIn("intent.expectedId == 0x3E9", POLICY)
        self.assertNotIn("kHandsOnCandidateId, frame", DASH)

    def test_observation_filters_and_can_b_scheduler_exist(self):
        self.assertEqual(HANDLERS.count("0x247"), 3)
        self.assertEqual(HANDLERS.count("0x3E9"), 3)
        self.assertIn("dashCanBTxScheduler.prepare", DASH)
        self.assertIn("CAN_BUS_CH, CAN_BUS_CAN_B", DASH)
        self.assertIn("TxControl::RequireAssistActivity", DASH)
        self.assertIn("TxControl::RequireSummonEligible", DASH)
        self.assertIn("ExperimentalAssist::prepareUlcEcho(config, observed, modified)", DASH)
        self.assertIn("ExperimentalAssist::prepareSummonEuHw4Echo(config, observed, modified)", DASH)
        self.assertIn("appDashboardActivityTxAllowed = dashActivityTxAllowed", DASH)
        self.assertIn("if (!apInjectionGate)", DASH)
        self.assertIn("appDashboardActivityTxAllowed && !appDashboardActivityTxAllowed()", APP)
        self.assertIn('"activity_gate_blocked"', APP)

    def test_summon_policy_cannot_use_the_legacy_ap_gate_bypass(self):
        self.assertIn("context.summonEligible = summonEligible;", DASH)
        self.assertNotIn(
            "context.summonEligible = !static_cast<bool>(apInjectionGate) || summonEligible;",
            DASH,
        )

    def test_hw4_summon_has_no_all_cars_catalog_bypass(self):
        self.assertNotIn("Summon EU Unlock for all cars", UI)

    def test_fsd_catalog_rules_are_chassis_only(self):
        self.assertEqual(UI.count("id:1021,bus:'CH'"), 4)


if __name__ == "__main__":
    unittest.main()
