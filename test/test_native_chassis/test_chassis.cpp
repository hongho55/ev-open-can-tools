#include <initializer_list>

#include <unity.h>
#include "chassis/fsd_state.h"
#include "drivers/dual_can_routing.h"

using namespace Chassis;
void setUp() {}
void tearDown() {}

static CanFrame das(uint32_t id = kDasLegacyHw3Id)
{
    CanFrame frame;
    frame.id = id;
    frame.bus = DualCanRouting::canBBusLabel();
    frame.data[0] = 0xA3;
    frame.data[1] = 0x6F;
    return frame;
}

void test_bus_filter_exhaustive()
{
    for (unsigned bus = 0; bus < 256; ++bus)
    {
        auto frame = das();
        frame.bus = bus;
        // Enumerate all valid RX label combinations, independently of predicate.
        const bool accepted = bus == CAN_BUS_CH || bus == CAN_BUS_VEH ||
            bus == (CAN_BUS_CH | CAN_BUS_VEH) || bus == CAN_BUS_CAN_B ||
            bus == (CAN_BUS_CAN_B | CAN_BUS_CH) ||
            bus == (CAN_BUS_CAN_B | CAN_BUS_VEH) ||
            bus == DualCanRouting::canBBusLabel();
        ChassisCapability cap(DasLayout::LegacyHw3, 100);
        ChassisFsdState state(DasLayout::LegacyHw3, 100);
        TEST_ASSERT_EQUAL(accepted, cap.observe(frame, 0));
        TEST_ASSERT_EQUAL(accepted, state.observe(frame, 0));
        TEST_ASSERT_EQUAL(accepted, cap.dasFresh(0));
        TEST_ASSERT_EQUAL(accepted, state.sample(0).fresh);
    }
}

void test_only_selected_das_id_counts_never_370()
{
    for (auto layout : {DasLayout::Unknown, DasLayout::LegacyHw3, DasLayout::StandardHw4})
    {
        for (uint32_t id = 0; id <= 0x7FF; ++id)
        {
            ChassisCapability cap(layout, 100);
            ChassisFsdState state(layout, 100);
            const bool accepted = (layout == DasLayout::LegacyHw3 && id == 0x399) ||
                (layout == DasLayout::StandardHw4 && id == 0x39B);
            TEST_ASSERT_EQUAL(accepted, cap.observe(das(id), 1));
            TEST_ASSERT_EQUAL(accepted, state.observe(das(id), 1));
            TEST_ASSERT_EQUAL(accepted, cap.dasFresh(1));
        }
    }
    ChassisCapability cap(DasLayout::LegacyHw3, 100);
    TEST_ASSERT_FALSE(cap.observe(das(0x80000399u), 1));
}

void test_timeout_refresh_and_wrap()
{
    ChassisCapability cap(DasLayout::LegacyHw3, 100);
    ChassisFsdState state(DasLayout::LegacyHw3, 100);
    TEST_ASSERT_FALSE(cap.dasFresh(0));
    TEST_ASSERT_FALSE(state.sample(0).fresh);
    cap.observe(das(), 0xFFFFFFF0u);
    state.observe(das(), 0xFFFFFFF0u);
    TEST_ASSERT_TRUE(cap.dasFresh(0x53)); // elapsed 99
    TEST_ASSERT_TRUE(state.sample(0x53).fresh);
    TEST_ASSERT_FALSE(cap.dasFresh(0x54)); // exact expiry
    TEST_ASSERT_FALSE(state.sample(0x54).fresh);
    TEST_ASSERT_EQUAL_UINT8(0, state.sample(0x54).raw);
    cap.observe(das(), 0x54);
    state.observe(das(), 0x54);
    TEST_ASSERT_TRUE(cap.dasFresh(0x54));
    TEST_ASSERT_TRUE(state.sample(0x54).fresh);
    cap.reset();
    state.reset();
    TEST_ASSERT_FALSE(cap.dasFresh(0x54));
    TEST_ASSERT_FALSE(state.sample(0x54).fresh);
}

void test_rejected_frames_do_not_refresh_or_overwrite()
{
    ChassisCapability cap(DasLayout::LegacyHw3, 10);
    ChassisFsdState state(DasLayout::LegacyHw3, 10);
    cap.observe(das(), 0);
    state.observe(das(), 0);
    for (unsigned dlc = 0; dlc < 256; ++dlc)
    {
        if (dlc == 8) continue;
        auto frame = das();
        frame.dlc = dlc;
        frame.data[0] = 9;
        TEST_ASSERT_FALSE(cap.observe(frame, 9));
        TEST_ASSERT_FALSE(state.observe(frame, 9));
    }
    auto party = das();
    party.bus = DualCanRouting::canABusLabel();
    TEST_ASSERT_FALSE(cap.observe(party, 9));
    TEST_ASSERT_FALSE(state.observe(party, 9));
    TEST_ASSERT_FALSE(cap.observe(das(0x370), 9));
    TEST_ASSERT_FALSE(state.observe(das(0x370), 9));
    TEST_ASSERT_EQUAL_UINT8(3, state.sample(9).raw);
    TEST_ASSERT_EQUAL_UINT32(0, state.sample(9).observedMs);
    TEST_ASSERT_FALSE(cap.dasFresh(10));
    TEST_ASSERT_FALSE(state.sample(10).fresh);
}

void test_layout_extraction_and_disabled_defaults()
{
    ChassisCapability defaultCap;
    ChassisFsdState defaultState;
    TEST_ASSERT_FALSE(defaultCap.observe(das(), 1));
    TEST_ASSERT_FALSE(defaultState.observe(das(), 1));
    for (uint32_t timeout : {0u, 0x80000000u, 0xFFFFFFFFu})
    {
        ChassisCapability cap(DasLayout::LegacyHw3, timeout);
        ChassisFsdState state(DasLayout::LegacyHw3, timeout);
        cap.observe(das(), 1);
        state.observe(das(), 1);
        TEST_ASSERT_FALSE(cap.dasFresh(1));
        TEST_ASSERT_FALSE(state.sample(1).fresh);
    }
    ChassisFsdState hw4(DasLayout::StandardHw4, 100);
    TEST_ASSERT_FALSE(hw4.observe(das(0x399), 1));
    TEST_ASSERT_TRUE(hw4.observe(das(0x39B), 2));
    TEST_ASSERT_EQUAL_UINT8(3, hw4.sample(2).raw);
    TEST_ASSERT_EQUAL_UINT32(2, hw4.sample(2).observedMs);
    // Every nibble is raw data, including unknown/reserved values; no active flag.
    for (unsigned raw = 0; raw < 16; ++raw)
    {
        auto frame = das(0x39B);
        frame.data[0] = 0xA0 | raw;
        hw4.observe(frame, 3);
        TEST_ASSERT_EQUAL_UINT8(raw, hw4.sample(3).raw);
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_bus_filter_exhaustive);
    RUN_TEST(test_only_selected_das_id_counts_never_370);
    RUN_TEST(test_timeout_refresh_and_wrap);
    RUN_TEST(test_rejected_frames_do_not_refresh_or_overwrite);
    RUN_TEST(test_layout_extraction_and_disabled_defaults);
    return UNITY_END();
}
