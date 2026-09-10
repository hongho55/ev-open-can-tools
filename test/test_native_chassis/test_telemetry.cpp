#include <initializer_list>

#include <unity.h>
#include "chassis/telemetry.h"
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
        kDasControlId, kDasStatus2Id, kApLegacyId, kApControlId,
        kDasSteeringId,
    };
    for (uint32_t id : ids)
        TEST_ASSERT_TRUE(telemetry.observe(frame(id), 1));

    TEST_ASSERT_FALSE(telemetry.observe(frame(0x370), 2));
    TEST_ASSERT_FALSE(telemetry.observe(frame(0x123), 2));
    TEST_ASSERT_FALSE(telemetry.observe(frame(0x80000129u), 2));
    TEST_ASSERT_EQUAL_UINT32(9, telemetry.acceptedFrames());
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
    TEST_ASSERT_FALSE(telemetry.observe(frame(kDasSteeringId, 2), 1));
    TEST_ASSERT_FALSE(telemetry.observe(frame(kApControlId, 7), 1));

    TEST_ASSERT_TRUE(telemetry.observe(frame(kSteeringAngleId, 4), 2));
    TEST_ASSERT_TRUE(telemetry.observe(frame(kDasSteeringId, 3), 2));
    TEST_ASSERT_EQUAL_UINT32(2, telemetry.acceptedFrames());
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
    RUN_TEST(test_samples_expire_wrap_safely_and_reset);
    RUN_TEST(test_invalid_timeout_never_reports_live);
    return UNITY_END();
}
