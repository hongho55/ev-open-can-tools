#include <initializer_list>

#include <unity.h>
#include "chassis/telemetry.h"
#include "chassis/telemetry_state.h"
#include "chassis/global_tx_safety_state.h"
#include "drivers/dual_can_routing.h"

using namespace Chassis;
void setUp() {}
void tearDown() {}

static CanFrame frame(uint32_t id, uint8_t dlc = 8,
                      uint8_t bus = DualCanRouting::canBBusLabel())
{
    CanFrame result;
    result.id = id;
    result.dlc = dlc;
    result.bus = bus;
    return result;
}

void test_whitelist_excludes_party_and_unknown_ids()
{
    ChassisTelemetry telemetry(DasLayout::LegacyHw3, 100);
    const uint32_t ids[] = {
        kDiSystemStatusId, kSteeringAngleId, kEspStatusId, kUiMapDataId,
        kDasControlId, kDasStatus2Id, kDasSettingsId, kApLegacyId, kApControlId,
        kDasSteeringId,
    };
    for (uint32_t id : ids)
        TEST_ASSERT_TRUE(telemetry.observe(frame(id), 1));

    TEST_ASSERT_FALSE(telemetry.observe(frame(0x370), 2));
    TEST_ASSERT_FALSE(telemetry.observe(frame(0x123), 2));
    TEST_ASSERT_FALSE(telemetry.observe(frame(0x80000129u), 2));
    TEST_ASSERT_EQUAL_UINT32(10, telemetry.acceptedFrames());
}

void test_only_chassis_bus_labels_are_accepted()
{
    const uint8_t buses[] = {
        CAN_BUS_CH,
        CAN_BUS_VEH,
        CAN_BUS_CH | CAN_BUS_VEH,
        CAN_BUS_CAN_B,
        CAN_BUS_CAN_B | CAN_BUS_CH,
        CAN_BUS_CAN_B | CAN_BUS_VEH,
        DualCanRouting::canBBusLabel(),
    };
    for (uint8_t bus : buses)
    {
        ChassisTelemetry telemetry(DasLayout::LegacyHw3, 100);
        TEST_ASSERT_TRUE(telemetry.observe(frame(kDasLegacyHw3Id, 8, bus), 1));
    }

    const uint8_t rejectedBuses[] = {
        uint8_t(CAN_BUS_ANY), uint8_t(CAN_BUS_PARTY), uint8_t(CAN_BUS_CAN_A),
        uint8_t(CAN_BUS_CAN_A | CAN_BUS_PARTY), 0x80u,
    };
    for (uint8_t bus : rejectedBuses)
    {
        ChassisTelemetry telemetry(DasLayout::LegacyHw3, 100);
        TEST_ASSERT_FALSE(telemetry.observe(frame(kDasLegacyHw3Id, 8, bus), 1));
    }
}

void test_das_layout_is_explicit_and_370_never_counts()
{
    ChassisTelemetry legacy(DasLayout::LegacyHw3, 100);
    ChassisTelemetry hw4(DasLayout::StandardHw4, 100);
    ChassisTelemetry unknown(DasLayout::Unknown, 100);

    TEST_ASSERT_TRUE(legacy.observe(frame(kDasLegacyHw3Id), 1));
    TEST_ASSERT_FALSE(legacy.observe(frame(kDasHw4Id), 1));
    TEST_ASSERT_FALSE(hw4.observe(frame(kDasLegacyHw3Id), 1));
    TEST_ASSERT_TRUE(hw4.observe(frame(kDasHw4Id), 1));
    TEST_ASSERT_FALSE(unknown.observe(frame(kDasLegacyHw3Id), 1));
    TEST_ASSERT_FALSE(unknown.observe(frame(kDasHw4Id), 1));
    TEST_ASSERT_FALSE(legacy.observe(frame(0x370), 1));
}

void test_unknown_or_short_dlc_is_rejected()
{
    ChassisTelemetry telemetry(DasLayout::LegacyHw3, 100);
    TEST_ASSERT_FALSE(telemetry.observe(frame(kDasLegacyHw3Id, 7), 1));
    TEST_ASSERT_FALSE(telemetry.observe(frame(kDasLegacyHw3Id, 9), 1));
    TEST_ASSERT_FALSE(telemetry.observe(frame(kSteeringAngleId, 3), 1));
    TEST_ASSERT_FALSE(telemetry.observe(frame(kEspStatusId, 3), 1));
    TEST_ASSERT_FALSE(telemetry.observe(frame(kUiMapDataId, 1), 1));
    TEST_ASSERT_FALSE(telemetry.observe(frame(kDasControlId, 2), 1));
    TEST_ASSERT_FALSE(telemetry.observe(frame(kDasStatus2Id, 4), 1));
    TEST_ASSERT_FALSE(telemetry.observe(frame(kDasSettingsId, 4), 1));
    TEST_ASSERT_FALSE(telemetry.observe(frame(kDasSteeringId, 2), 1));
    TEST_ASSERT_FALSE(telemetry.observe(frame(kApControlId, 7), 1));

    TEST_ASSERT_TRUE(telemetry.observe(frame(kSteeringAngleId, 4), 2));
    TEST_ASSERT_TRUE(telemetry.observe(frame(kDasSteeringId, 3), 2));
    TEST_ASSERT_EQUAL_UINT32(2, telemetry.acceptedFrames());
}

void test_decoder_fields_match_flipper_layouts()
{
    Chassis::TelemetryState telemetry(DasLayout::StandardHw4, 100);

    auto brake = frame(kEspStatusId, 4);
    brake.data[3] = 0x20; // ESP_driverBrakeApply raw 1: Not_Applied
    TEST_ASSERT_TRUE(telemetry.observe(brake, 1));
    auto snapshot = telemetry.snapshot(1);
    TEST_ASSERT_TRUE(snapshot.brakeSeen);
    TEST_ASSERT_FALSE(snapshot.brakeApplied);

    brake.data[3] = 0x40; // raw 2: Driver_applying_brakes
    TEST_ASSERT_TRUE(telemetry.observe(brake, 2));
    snapshot = telemetry.snapshot(2);
    TEST_ASSERT_TRUE(snapshot.brakeApplied);

    auto dasControl = frame(kDasControlId, 3);
    dasControl.data[0] = 0xA5; // set speed low bits and unrelated byte-0 nibble
    dasControl.data[1] = 0x40; // DAS_accState = 4
    TEST_ASSERT_TRUE(telemetry.observe(dasControl, 3));
    snapshot = telemetry.snapshot(3);
    TEST_ASSERT_TRUE(snapshot.dasControlSeen);
    TEST_ASSERT_EQUAL_UINT8(4, snapshot.accState);
    TEST_ASSERT_FALSE(snapshot.dasStatus2Seen);

    auto dasStatus2 = frame(kDasStatus2Id, 5);
    dasStatus2.data[0] = 0x1F; // unrelated low byte
    dasStatus2.data[1] = 0x40; // activationFailureStatus = 1
    dasStatus2.data[3] = 0x14; // DAS_ACC_report = 5
    TEST_ASSERT_TRUE(telemetry.observe(dasStatus2, 4));
    snapshot = telemetry.snapshot(4);
    TEST_ASSERT_TRUE(snapshot.dasStatus2Seen);
    TEST_ASSERT_EQUAL_UINT8(5, snapshot.accReport);
    TEST_ASSERT_EQUAL_UINT8(1, snapshot.activationFailureStatus);

    auto dasSettings = frame(kDasSettingsId, 5);
    dasSettings.data[4] = 0x40; // DAS_autosteerEnabled = 1
    TEST_ASSERT_TRUE(telemetry.observe(dasSettings, 5));
    snapshot = telemetry.snapshot(5);
    TEST_ASSERT_TRUE(snapshot.dasSettingsSeen);
    TEST_ASSERT_TRUE(snapshot.autosteerEnabled);

    auto di = frame(kDiSystemStatusId, 7);
    di.data[kDiTrackModeByte] = 0x02;
    di.data[kDiTractionControlByte] = 0x05;
    TEST_ASSERT_TRUE(telemetry.observe(di, 6));
    snapshot = telemetry.snapshot(6);
    TEST_ASSERT_TRUE(snapshot.diModesSeen);
    TEST_ASSERT_EQUAL_UINT8(2, snapshot.trackModeState);
    TEST_ASSERT_EQUAL_UINT8(5, snapshot.tractionControlMode);
}

void test_right_stalk_is_read_only_exact_dlc_and_stale_safe()
{
    Chassis::TelemetryState telemetry(DasLayout::StandardHw4, 100);
    auto stalk = frame(0x229, 3, CAN_BUS_VEH);
    stalk.data[0] = 0xAA; // observed CRC byte; validity is intentionally unknown
    stalk.data[1] = 0x39; // counter 9, right-stalk status 3
    stalk.data[2] = 0x01; // park button pressed

    TEST_ASSERT_TRUE(telemetry.observe(stalk, 10));
    auto snapshot = telemetry.snapshot(10);
    TEST_ASSERT_TRUE(snapshot.rightStalkSeen);
    TEST_ASSERT_EQUAL_HEX8(0xAA, snapshot.rightStalkCrc);
    TEST_ASSERT_EQUAL_UINT8(9, snapshot.rightStalkCounter);
    TEST_ASSERT_EQUAL_UINT8(3, snapshot.rightStalkStatus);
    TEST_ASSERT_EQUAL_UINT8(1, snapshot.parkButtonStatus);

    TEST_ASSERT_FALSE(telemetry.observe(frame(0x229, 2, CAN_BUS_VEH), 11));
    TEST_ASSERT_FALSE(telemetry.observe(frame(0x229, 4, CAN_BUS_VEH), 11));
    TEST_ASSERT_FALSE(telemetry.snapshot(110).rightStalkSeen);
}

void test_hw4_ap_state_uses_byte0_low_nibble()
{
    Chassis::TelemetryState telemetry(DasLayout::StandardHw4, 100);
    auto das = frame(kDasHw4Id, 8);
    das.data[0] = 0x03; // standard HW4 ACTIVE_NOMINAL
    das.data[1] = 0x60; // adjacent warning fields must be ignored
    TEST_ASSERT_TRUE(telemetry.observe(das, 1));
    auto snapshot = telemetry.snapshot(1);
    TEST_ASSERT_TRUE(snapshot.dasSeen);
    TEST_ASSERT_EQUAL_UINT8(3, snapshot.apState);
}

void test_hw4_ap_state_accepts_observed_can_a_topology_only_for_39b()
{
    Chassis::TelemetryState telemetry(DasLayout::StandardHw4, 100);
    auto das = frame(kDasHw4Id, 8, DualCanRouting::canABusLabel());
    das.physicalBus = CAN_BUS_CAN_A;
    das.data[0] = 0x03;
    TEST_ASSERT_TRUE(telemetry.observe(das, 1));
    TEST_ASSERT_EQUAL_UINT8(3, telemetry.snapshot(1).apState);

    auto missingProvenance = das;
    missingProvenance.physicalBus = CAN_BUS_ANY;
    TEST_ASSERT_FALSE(telemetry.observe(missingProvenance, 2));

    auto wrongPhysicalBus = das;
    wrongPhysicalBus.physicalBus = CAN_BUS_CAN_B;
    TEST_ASSERT_FALSE(telemetry.observe(wrongPhysicalBus, 3));

    auto unrelatedPartyFrame = frame(kDiSystemStatusId, 8,
                                     DualCanRouting::canABusLabel());
    unrelatedPartyFrame.physicalBus = CAN_BUS_CAN_A;
    TEST_ASSERT_FALSE(telemetry.observe(unrelatedPartyFrame, 4));
}

void test_highland_layout_alias_uses_the_same_byte0_field()
{
    Chassis::TelemetryState telemetry(DasLayout::HighlandHw4Byte0, 100);
    auto das = frame(kDasHw4Id, 8);
    das.data[0] = 0x06;
    das.data[1] = 0x30; // compatibility alias must use the same byte-0 field
    TEST_ASSERT_TRUE(telemetry.observe(das, 1));
    auto snapshot = telemetry.snapshot(1);
    TEST_ASSERT_TRUE(snapshot.dasSeen);
    TEST_ASSERT_EQUAL_UINT8(6, snapshot.apState);
}

void test_di_autopark_requires_can_b_provenance_and_clears_explicitly()
{
    Chassis::TelemetryState telemetry(DasLayout::StandardHw4, 100);

    auto missingProvenance = frame(0x286, 8, DualCanRouting::canBBusLabel());
    missingProvenance.data[3] = static_cast<uint8_t>(3U << 1);
    TEST_ASSERT_FALSE(telemetry.observe(missingProvenance, 1));
    TEST_ASSERT_FALSE(telemetry.snapshot(1).diStateSeen);

    auto wrongBus = missingProvenance;
    wrongBus.bus = DualCanRouting::canABusLabel();
    wrongBus.physicalBus = CAN_BUS_CAN_A;
    TEST_ASSERT_FALSE(telemetry.observe(wrongBus, 2));

    auto active = missingProvenance;
    active.physicalBus = CAN_BUS_CAN_B;
    for (uint8_t state : {3u, 4u, 9u})
    {
        active.data[3] = static_cast<uint8_t>(state << 1);
        TEST_ASSERT_TRUE(telemetry.observe(active, 10 + state));
        const auto snapshot = telemetry.snapshot(10 + state);
        TEST_ASSERT_TRUE(snapshot.diStateSeen);
        TEST_ASSERT_EQUAL_UINT8(state, snapshot.autoparkState);
        TEST_ASSERT_TRUE(snapshot.autoparkActive);
    }

    TEST_ASSERT_TRUE(telemetry.snapshot(50).autoparkActive);
    auto clear = active;
    clear.data[3] = 0;
    TEST_ASSERT_TRUE(telemetry.observe(clear, 60));
    const auto cleared = telemetry.snapshot(60);
    TEST_ASSERT_TRUE(cleared.diStateSeen);
    TEST_ASSERT_EQUAL_UINT8(0, cleared.autoparkState);
    TEST_ASSERT_FALSE(cleared.autoparkActive);
    TEST_ASSERT_FALSE(telemetry.snapshot(160).diStateSeen);
}

void test_samples_expire_wrap_safely_and_reset()
{
    ChassisTelemetry telemetry(DasLayout::LegacyHw3, 100);
    TEST_ASSERT_FALSE(telemetry.sample(TelemetrySignal::SteeringAngle, 0).fresh);
    TEST_ASSERT_TRUE(telemetry.observe(frame(kSteeringAngleId, 4), 0xFFFFFFF0u));

    auto sample = telemetry.sample(TelemetrySignal::SteeringAngle, 0x53u);
    TEST_ASSERT_TRUE(sample.fresh); // elapsed 99 ms across uint32 wrap
    TEST_ASSERT_EQUAL_UINT32(1, sample.count);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFF0u, sample.lastObservedMs);
    TEST_ASSERT_FALSE(telemetry.sample(TelemetrySignal::SteeringAngle, 0x54u).fresh);
    TEST_ASSERT_FALSE(telemetry.sample(TelemetrySignal::EspStatus, 0x54u).fresh);

    telemetry.reset();
    TEST_ASSERT_EQUAL_UINT32(0, telemetry.acceptedFrames());
    TEST_ASSERT_FALSE(telemetry.sample(TelemetrySignal::SteeringAngle, 0x54u).fresh);
}

void test_vehicle_ota_requires_full_byte_stability_and_explicit_clear()
{
    TelemetryState telemetry(DasLayout::StandardHw4, 100);
    auto ota = frame(0x318, 8);
    ota.physicalBus = CAN_BUS_CAN_B;

    auto party = ota;
    party.bus = DualCanRouting::canABusLabel();
    party.physicalBus = CAN_BUS_CAN_A;
    party.data[6] = 0x02;
    TEST_ASSERT_FALSE(telemetry.observe(party, 0));
    TEST_ASSERT_FALSE(telemetry.snapshot(0).vehicleOtaSeen);

    // Low bits say "installing", but a changing upper rolling value must not
    // assert the global inhibit.
    for (uint8_t raw : {0x02u, 0x06u, 0x0Au, 0x0Eu})
    {
        ota.data[6] = raw;
        TEST_ASSERT_TRUE(telemetry.observe(ota, raw));
        TEST_ASSERT_FALSE(telemetry.snapshot(raw).vehicleOtaInProgress);
    }

    ota.data[6] = 0x12;
    for (uint8_t i = 0; i < 2; ++i)
        TEST_ASSERT_TRUE(telemetry.observe(ota, 20 + i));
    TEST_ASSERT_FALSE(telemetry.snapshot(21).vehicleOtaInProgress);
    TEST_ASSERT_TRUE(telemetry.observe(ota, 22));
    TEST_ASSERT_TRUE(telemetry.snapshot(22).vehicleOtaInProgress);
    TEST_ASSERT_EQUAL_HEX8(0x12, telemetry.snapshot(22).vehicleOtaByte6);

    // Installing samples with a changing full byte cannot assert a fresh
    // episode and must never clear an already asserted inhibit.
    for (uint8_t raw : {0x06u, 0x0Au, 0x0Eu, 0x12u, 0x16u, 0x1Au})
    {
        ota.data[6] = raw;
        TEST_ASSERT_TRUE(telemetry.observe(ota, 24 + raw));
        TEST_ASSERT_TRUE(telemetry.snapshot(24 + raw).vehicleOtaInProgress);
    }

    // CAN A/Party samples are never authoritative and cannot clear CAN B OTA.
    party.data[6] = 0x01;
    for (uint8_t i = 0; i < 6; ++i)
        TEST_ASSERT_FALSE(telemetry.observe(party, 70 + i));
    TEST_ASSERT_TRUE(telemetry.snapshot(75).vehicleOtaInProgress);

    // Six explicit consecutive non-installing CAN B samples clear the latch;
    // upper bits may roll during normal operation.
    for (uint8_t i = 0; i < 5; ++i)
    {
        ota.data[6] = static_cast<uint8_t>(0x01U + (i << 2));
        TEST_ASSERT_TRUE(telemetry.observe(ota, 30 + i));
    }
    TEST_ASSERT_TRUE(telemetry.snapshot(34).vehicleOtaInProgress);
    ota.data[6] = 0x15;
    TEST_ASSERT_TRUE(telemetry.observe(ota, 35));
    TEST_ASSERT_FALSE(telemetry.snapshot(35).vehicleOtaInProgress);
    TEST_ASSERT_TRUE(telemetry.snapshot(134).vehicleOtaSeen);
    TEST_ASSERT_TRUE(telemetry.snapshot(134).vehicleOtaFresh);
    TEST_ASSERT_TRUE(telemetry.snapshot(135).vehicleOtaSeen);
    TEST_ASSERT_FALSE(telemetry.snapshot(135).vehicleOtaFresh);

    telemetry.reset();
    const auto reset = telemetry.snapshot(40);
    TEST_ASSERT_FALSE(reset.vehicleOtaSeen);
    TEST_ASSERT_FALSE(reset.vehicleOtaFresh);
    TEST_ASSERT_FALSE(reset.vehicleOtaInProgress);
}

void test_global_tx_safety_decision_is_fail_closed()
{
    GlobalTxSafetyState gate(100);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(GlobalTxSafetyDecision::AutoparkOrDiStale),
        static_cast<uint8_t>(gate.decision(0)));

    CanFrame di = frame(0x286);
    di.dlc = 4;
    di.data[3] = static_cast<uint8_t>(6 << 1);
    di.bus = CAN_BUS_CAN_A | CAN_BUS_PARTY;
    di.physicalBus = CAN_BUS_CAN_A;
    TEST_ASSERT_FALSE(gate.observe(di, 9));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(GlobalTxSafetyDecision::AutoparkOrDiStale),
        static_cast<uint8_t>(gate.decision(9)));

    di.bus = CAN_BUS_CAN_B | CAN_BUS_CH | CAN_BUS_VEH;
    di.physicalBus = CAN_BUS_CAN_B;
    TEST_ASSERT_TRUE(gate.observe(di, 10));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GlobalTxSafetyDecision::Allowed),
                            static_cast<uint8_t>(gate.decision(109)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(GlobalTxSafetyDecision::AutoparkOrDiStale),
        static_cast<uint8_t>(gate.decision(110)));

    for (const uint8_t blockedState : {3u, 4u, 9u})
    {
        di.data[3] = static_cast<uint8_t>(blockedState << 1);
        TEST_ASSERT_TRUE(gate.observe(di, 120 + blockedState));
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(GlobalTxSafetyDecision::AutoparkOrDiStale),
            static_cast<uint8_t>(gate.decision(120 + blockedState)));
    }

    di.data[3] = static_cast<uint8_t>(6 << 1);
    TEST_ASSERT_TRUE(gate.observe(di, 140));
    CanFrame ota = frame(0x318);
    ota.bus = CAN_BUS_CAN_B | CAN_BUS_CH | CAN_BUS_VEH;
    ota.physicalBus = CAN_BUS_CAN_B;
    ota.dlc = 7;
    ota.data[6] = 0x12;
    TEST_ASSERT_TRUE(gate.observe(ota, 141));
    TEST_ASSERT_TRUE(gate.observe(ota, 142));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GlobalTxSafetyDecision::Allowed),
                            static_cast<uint8_t>(gate.decision(142)));
    TEST_ASSERT_TRUE(gate.observe(ota, 143));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GlobalTxSafetyDecision::VehicleOta),
                            static_cast<uint8_t>(gate.decision(143)));

    CanFrame partyClear = ota;
    partyClear.bus = CAN_BUS_CAN_A | CAN_BUS_PARTY;
    partyClear.physicalBus = CAN_BUS_CAN_A;
    partyClear.data[6] = 0x01;
    for (uint32_t i = 0; i < 6; ++i)
        TEST_ASSERT_FALSE(gate.observe(partyClear, 144 + i));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GlobalTxSafetyDecision::VehicleOta),
                            static_cast<uint8_t>(gate.decision(149)));
}

void test_global_tx_safety_rejects_invalid_timeout_and_clock_wrap()
{
    CanFrame di = frame(0x286);
    di.bus = CAN_BUS_CAN_B | CAN_BUS_CH | CAN_BUS_VEH;
    di.physicalBus = CAN_BUS_CAN_B;
    di.dlc = 4;
    di.data[3] = static_cast<uint8_t>(6 << 1);

    for (const uint32_t timeout : {0u, 0x80000000u, 0xFFFFFFFFu})
    {
        GlobalTxSafetyState invalid(timeout);
        TEST_ASSERT_TRUE(invalid.observe(di, 10));
        TEST_ASSERT_EQUAL_UINT8(
            static_cast<uint8_t>(GlobalTxSafetyDecision::AutoparkOrDiStale),
            static_cast<uint8_t>(invalid.decision(10)));
    }

    GlobalTxSafetyState wrapped(100);
    TEST_ASSERT_TRUE(wrapped.observe(di, 0xFFFFFFF0U));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GlobalTxSafetyDecision::Allowed),
                            static_cast<uint8_t>(wrapped.decision(0xFFFFFFF5U)));
    wrapped.advanceClock(5);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(GlobalTxSafetyDecision::AutoparkOrDiStale),
        static_cast<uint8_t>(wrapped.decision(5)));
    TEST_ASSERT_TRUE(wrapped.observe(di, 6));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GlobalTxSafetyDecision::Allowed),
                            static_cast<uint8_t>(wrapped.decision(7)));
}

void test_invalid_timeout_never_reports_live()
{
    for (uint32_t timeout : {0u, 0x80000000u, 0xFFFFFFFFu})
    {
        ChassisTelemetry telemetry(DasLayout::LegacyHw3, timeout);
        TEST_ASSERT_TRUE(telemetry.observe(frame(kDasLegacyHw3Id), 1));
        TEST_ASSERT_FALSE(telemetry.sample(TelemetrySignal::DasLegacyStatus, 1).fresh);
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_whitelist_excludes_party_and_unknown_ids);
    RUN_TEST(test_only_chassis_bus_labels_are_accepted);
    RUN_TEST(test_das_layout_is_explicit_and_370_never_counts);
    RUN_TEST(test_unknown_or_short_dlc_is_rejected);
    RUN_TEST(test_decoder_fields_match_flipper_layouts);
    RUN_TEST(test_right_stalk_is_read_only_exact_dlc_and_stale_safe);
    RUN_TEST(test_hw4_ap_state_uses_byte0_low_nibble);
    RUN_TEST(test_hw4_ap_state_accepts_observed_can_a_topology_only_for_39b);
    RUN_TEST(test_highland_layout_alias_uses_the_same_byte0_field);
    RUN_TEST(test_di_autopark_requires_can_b_provenance_and_clears_explicitly);
    RUN_TEST(test_vehicle_ota_requires_full_byte_stability_and_explicit_clear);
    RUN_TEST(test_global_tx_safety_decision_is_fail_closed);
    RUN_TEST(test_global_tx_safety_rejects_invalid_timeout_and_clock_wrap);
    RUN_TEST(test_samples_expire_wrap_safely_and_reset);
    RUN_TEST(test_invalid_timeout_never_reports_live);
    return UNITY_END();
}
