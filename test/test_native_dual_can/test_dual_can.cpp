#include <unity.h>

#include "drivers/dual_can_routing.h"
#include "gvret_protocol.h"

void setUp() {}
void tearDown() {}

void test_any_defaults_to_can_a_only()
{
    const auto targets = DualCanRouting::targetsForBus(CAN_BUS_ANY);
    TEST_ASSERT_TRUE(targets.canA);
    TEST_ASSERT_FALSE(targets.canB);
}

void test_semantic_masks_keep_existing_bus_mapping()
{
    TEST_ASSERT_TRUE(DualCanRouting::targetsForBus(CAN_BUS_PARTY).canA);
    TEST_ASSERT_FALSE(DualCanRouting::targetsForBus(CAN_BUS_PARTY).canB);
    TEST_ASSERT_FALSE(DualCanRouting::targetsForBus(CAN_BUS_VEH).canA);
    TEST_ASSERT_TRUE(DualCanRouting::targetsForBus(CAN_BUS_VEH | CAN_BUS_CH).canB);
}

void test_semantic_and_physical_bus_labels_stay_distinct()
{
    CanFrame canA = {.bus = DualCanRouting::canABusLabel(), .physicalBus = CAN_BUS_CAN_A};
    TEST_ASSERT_TRUE(DualCanRouting::targetsForBus(canA.bus).canA);
    TEST_ASSERT_FALSE(DualCanRouting::targetsForBus(canA.bus).canB);
    TEST_ASSERT_EQUAL_UINT8(CAN_BUS_CAN_A, canA.physicalBus);
    TEST_ASSERT_EQUAL_UINT8(0, GvretProtocol::busIndex(canA));

    CanFrame canB = {.bus = DualCanRouting::canBBusLabel(), .physicalBus = CAN_BUS_CAN_B};
    TEST_ASSERT_FALSE(DualCanRouting::targetsForBus(canB.bus).canA);
    TEST_ASSERT_TRUE(DualCanRouting::targetsForBus(canB.bus).canB);
    TEST_ASSERT_EQUAL_UINT8(CAN_BUS_CAN_B, canB.physicalBus);
    TEST_ASSERT_EQUAL_UINT8(1, GvretProtocol::busIndex(canB));
}

void test_explicit_combined_mask_targets_both()
{
    auto targets = DualCanRouting::targetsForBus(CAN_BUS_CAN_A | CAN_BUS_CAN_B);
    TEST_ASSERT_TRUE(targets.canA);
    TEST_ASSERT_TRUE(targets.canB);

    targets = DualCanRouting::targetsForBus(CAN_BUS_PARTY | CAN_BUS_VEH);
    TEST_ASSERT_TRUE(targets.canA);
    TEST_ASSERT_TRUE(targets.canB);
}

void test_unknown_mask_fails_closed()
{
    const auto targets = DualCanRouting::targetsForBus(0x80);
    TEST_ASSERT_FALSE(targets.canA);
    TEST_ASSERT_FALSE(targets.canB);

    const auto mixedUnknown = DualCanRouting::targetsForBus(CAN_BUS_CAN_A | 0x80);
    TEST_ASSERT_FALSE(mixedUnknown.canA);
    TEST_ASSERT_FALSE(mixedUnknown.canB);
}

void test_receive_labels_preserve_semantic_and_physical_bus()
{
    TEST_ASSERT_EQUAL_UINT8(CAN_BUS_CAN_A | CAN_BUS_PARTY, DualCanRouting::canABusLabel());
    TEST_ASSERT_EQUAL_UINT8(CAN_BUS_CAN_B | CAN_BUS_CH | CAN_BUS_VEH,
                            DualCanRouting::canBBusLabel());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_any_defaults_to_can_a_only);
    RUN_TEST(test_semantic_masks_keep_existing_bus_mapping);
    RUN_TEST(test_semantic_and_physical_bus_labels_stay_distinct);
    RUN_TEST(test_explicit_combined_mask_targets_both);
    RUN_TEST(test_unknown_mask_fails_closed);
    RUN_TEST(test_receive_labels_preserve_semantic_and_physical_bus);
    return UNITY_END();
}
