#include <unity.h>
#include "can_frame_types.h"
#include "can_helpers.h"
#include "drivers/mock_driver.h"
#include "handlers.h"
#include "experimental/assist_controls.h"

static MockDriver mock;
static uint8_t onSendCount = 0;

static void countOnSend(uint8_t /*mux*/, bool /*ok*/)
{
    onSendCount++;
}

template <typename Handler>
static void prepareDashboardHandler(Handler &handler)
{
    handler.enablePrint = false;
    handler.onSend = countOnSend;
}

void setUp()
{
    mock.reset();
    onSendCount = 0;
    bypassTlsscRequirementRuntime = true;
    isaSpeedChimeSuppressRuntime = true;
    emergencyVehicleDetectionRuntime = true;
    enhancedAutopilotRuntime = true;
    nagKillerRuntime = true;
}

void tearDown() {}

void test_fsd_selection_uses_byte4_bit6()
{
    bypassTlsscRequirementRuntime = false;
    CanFrame frame = {};
    frame.data[4] = 0x20;
    TEST_ASSERT_FALSE(isADSelectedInUI(frame));
    frame.data[4] = 0x40;
    TEST_ASSERT_TRUE(isADSelectedInUI(frame));
}

void test_dashboard_legacy_mux0_observes_ad_without_injecting()
{
    LegacyHandler handler;
    prepareDashboardHandler(handler);

    CanFrame f = {.id = 1006};
    f.data[0] = 0x00;
    f.data[4] = 0x40;

    handler.handleMessage(f, mock);

    TEST_ASSERT_TRUE(handler.ADEnabled);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(0, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(0, onSendCount);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.data[5] & 0x40);
}

void test_dashboard_legacy_manual_profile_injects_mux0()
{
    LegacyHandler handler;
    prepareDashboardHandler(handler);
    handler.speedProfileAuto = false;
    handler.speedProfile = 2;

    CanFrame f = {.id = 1006};
    f.data[0] = 0x00;
    f.data[4] = 0x40;

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x04, mock.sent[0].data[6] & 0x06);
}

void test_dashboard_legacy_mux1_does_not_inject_nag_suppression()
{
    LegacyHandler handler;
    prepareDashboardHandler(handler);

    CanFrame f = {.id = 1006};
    f.data[0] = 0x01;
    setBit(f, 19, true);

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(0, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(0, onSendCount);
    TEST_ASSERT_TRUE((f.data[2] >> 3) & 0x01);
}

void test_dashboard_hw3_mux0_observes_state_without_injecting()
{
    HW3Handler handler;
    prepareDashboardHandler(handler);

    CanFrame f = {.id = 1021};
    f.data[0] = 0x00;
    f.data[3] = 60;
    f.data[4] = 0x40;

    handler.handleMessage(f, mock);

    TEST_ASSERT_TRUE(handler.ADEnabled);
    TEST_ASSERT_EQUAL_INT(0, handler.speedOffset);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(0, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(0, onSendCount);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.data[5] & 0x40);
}

void test_dashboard_hw3_manual_profile_injects_mux0()
{
    HW3Handler handler;
    prepareDashboardHandler(handler);
    handler.speedProfileAuto = false;
    handler.speedProfile = 2;

    CanFrame f = {.id = 1021};
    f.data[0] = 0x00;
    f.data[4] = 0x40;

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x04, mock.sent[0].data[6] & 0x06);
}

void test_dashboard_hw3_mux1_does_not_inject_nag_suppression()
{
    HW3Handler handler;
    prepareDashboardHandler(handler);

    CanFrame f = {.id = 1021};
    f.data[0] = 0x01;
    setBit(f, 19, true);

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(0, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(0, onSendCount);
    TEST_ASSERT_TRUE((f.data[2] >> 3) & 0x01);
}

void test_dashboard_hw4_mux0_observes_ad_without_injecting()
{
    HW4Handler handler;
    prepareDashboardHandler(handler);

    CanFrame f = {.id = 1021};
    f.data[0] = 0x00;
    f.data[4] = 0x40;

    handler.handleMessage(f, mock);

    TEST_ASSERT_TRUE(handler.ADEnabled);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(0, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(0, onSendCount);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.data[5] & 0x40);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.data[7] & 0x18);
}

void test_dashboard_hw4_manual_profile_injects_mux2()
{
    HW4Handler handler;
    prepareDashboardHandler(handler);
    handler.ADEnabled = true;
    handler.speedProfileAuto = false;
    handler.speedProfile = 4;

    CanFrame f = {.id = 1021};
    f.data[0] = 0x02;
    f.data[7] = 0x70;

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x80, mock.sent[0].data[7] & 0xE0);
}

void test_dashboard_hw4_mux1_does_not_inject_nag_suppression()
{
    HW4Handler handler;
    prepareDashboardHandler(handler);

    CanFrame f = {.id = 1021};
    f.data[0] = 0x01;
    setBit(f, 19, true);

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(0, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(0, onSendCount);
    TEST_ASSERT_TRUE((f.data[2] >> 3) & 0x01);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.data[5] & 0x80);
}

void test_dashboard_hw4_isa_suppression_does_not_inject()
{
    HW4Handler handler;
    prepareDashboardHandler(handler);

    CanFrame f = {.id = 921};
    f.data[1] = 0x00;

    handler.handleMessage(f, mock);

    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(0, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT8(0, onSendCount);
    TEST_ASSERT_EQUAL_HEX8(0x00, f.data[1] & 0x20);
}

void test_experimental_ulc_preserves_unselected_fields()
{
    ExperimentalAssist::Config config;
    config.ulcStalkConfirm = true;
    config.ulcOffHighway = true;
    config.ulcSpeedConfig = 2;
    config.ulcBlindSpotConfig = 1;
    CanFrame frame = {.id = 0x3F8, .dlc = 8};
    frame.data[0] = 0xA7;
    frame.data[1] = 0x01;
    frame.data[6] = 0xC3;
    frame.data[7] = 0x5A;

    TEST_ASSERT_TRUE(ExperimentalAssist::applyUlc(config, frame));
    TEST_ASSERT_EQUAL_HEX8(0xA5, frame.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x81, frame.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0xDB, frame.data[6]);
    TEST_ASSERT_EQUAL_HEX8(0x5A, frame.data[7]);
}

void test_experimental_ulc_disabled_is_noop()
{
    ExperimentalAssist::Config config;
    CanFrame frame = {.id = 0x3F8, .dlc = 8};
    frame.data[6] = 0xA5;
    TEST_ASSERT_FALSE(ExperimentalAssist::applyUlc(config, frame));
    TEST_ASSERT_EQUAL_HEX8(0xA5, frame.data[6]);
}

void test_experimental_summon_eu_is_mux1_flag_only()
{
    ExperimentalAssist::Config config;
    config.summonEuUnlock = true;
    CanFrame frame = {.id = 0x3FD, .dlc = 8};
    frame.data[0] = 1;
    setBit(frame, 19, true);

    TEST_ASSERT_TRUE(ExperimentalAssist::applySummonEuHw4(config, frame));
    TEST_ASSERT_FALSE((frame.data[2] >> 3) & 0x01);
    TEST_ASSERT_TRUE((frame.data[5] >> 7) & 0x01);

    frame.data[0] = 0;
    TEST_ASSERT_FALSE(ExperimentalAssist::applySummonEuHw4(config, frame));
}

void test_experimental_echo_requires_can_b_chassis_origin()
{
    ExperimentalAssist::Config config;
    config.ulcStalkConfirm = true;
    CanFrame observed = {.id = 0x3F8, .dlc = 8};
    observed.data[0] = 0x02;
    CanFrame modified;

    observed.bus = CAN_BUS_CAN_A | CAN_BUS_PARTY;
    observed.physicalBus = CAN_BUS_CAN_A;
    TEST_ASSERT_FALSE(ExperimentalAssist::prepareUlcEcho(config, observed, modified));

    observed.bus = CAN_BUS_CAN_B | CAN_BUS_PARTY;
    observed.physicalBus = CAN_BUS_CAN_B;
    TEST_ASSERT_FALSE(ExperimentalAssist::prepareUlcEcho(config, observed, modified));

    observed.bus = CAN_BUS_CAN_B | CAN_BUS_CH | CAN_BUS_VEH;
    TEST_ASSERT_TRUE(ExperimentalAssist::prepareUlcEcho(config, observed, modified));
}

void test_experimental_disabled_echo_never_prepares_tx()
{
    ExperimentalAssist::Config config;
    CanFrame observed = {.id = 0x3F8, .dlc = 8};
    observed.bus = CAN_BUS_CAN_B | CAN_BUS_CH | CAN_BUS_VEH;
    observed.physicalBus = CAN_BUS_CAN_B;
    CanFrame modified;

    TEST_ASSERT_FALSE(ExperimentalAssist::prepareUlcEcho(config, observed, modified));
    observed.id = 0x3FD;
    observed.data[0] = 1;
    TEST_ASSERT_FALSE(ExperimentalAssist::prepareSummonEuHw4Echo(config, observed, modified));
    TEST_ASSERT_EQUAL(0, mock.sent.size());
}

void test_hands_on_247_dry_run_never_mutates_or_transmits()
{
    ExperimentalAssist::HandsOn247DryRun dryRun;
    dryRun.setEnabled(true);
    CanFrame a = {.id = 0x247, .dlc = 2, .data = {0xA1, 0xB2}};
    CanFrame b = {.id = 0x3E9, .dlc = 1, .data = {0xCC}};

    TEST_ASSERT_TRUE(dryRun.observe(a, 100));
    TEST_ASSERT_TRUE(dryRun.observe(b, 150));
    const ExperimentalAssist::DryRunSnapshot state = dryRun.snapshot();
    TEST_ASSERT_TRUE(state.enabled);
    TEST_ASSERT_EQUAL_UINT32(1, state.frames247);
    TEST_ASSERT_EQUAL_UINT32(1, state.frames3e9);
    TEST_ASSERT_EQUAL_UINT32(1, state.nearbyEvents);
    TEST_ASSERT_EQUAL_HEX8(0xA1, a.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xCC, b.data[0]);
    TEST_ASSERT_EQUAL(0, mock.sent.size());

    dryRun.setEnabled(false);
    TEST_ASSERT_FALSE(dryRun.observe(a, 200));
    TEST_ASSERT_FALSE(dryRun.snapshot().enabled);
    TEST_ASSERT_EQUAL_UINT32(0, dryRun.snapshot().frames247);
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_fsd_selection_uses_byte4_bit6);
    RUN_TEST(test_dashboard_legacy_mux0_observes_ad_without_injecting);
    RUN_TEST(test_dashboard_legacy_manual_profile_injects_mux0);
    RUN_TEST(test_dashboard_legacy_mux1_does_not_inject_nag_suppression);
    RUN_TEST(test_dashboard_hw3_mux0_observes_state_without_injecting);
    RUN_TEST(test_dashboard_hw3_manual_profile_injects_mux0);
    RUN_TEST(test_dashboard_hw3_mux1_does_not_inject_nag_suppression);
    RUN_TEST(test_dashboard_hw4_mux0_observes_ad_without_injecting);
    RUN_TEST(test_dashboard_hw4_manual_profile_injects_mux2);
    RUN_TEST(test_dashboard_hw4_mux1_does_not_inject_nag_suppression);
    RUN_TEST(test_dashboard_hw4_isa_suppression_does_not_inject);
    RUN_TEST(test_experimental_ulc_preserves_unselected_fields);
    RUN_TEST(test_experimental_ulc_disabled_is_noop);
    RUN_TEST(test_experimental_summon_eu_is_mux1_flag_only);
    RUN_TEST(test_experimental_echo_requires_can_b_chassis_origin);
    RUN_TEST(test_experimental_disabled_echo_never_prepares_tx);
    RUN_TEST(test_hands_on_247_dry_run_never_mutates_or_transmits);

    return UNITY_END();
}
