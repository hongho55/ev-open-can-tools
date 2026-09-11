#include <initializer_list>
#include <limits>
#include <unity.h>
#include "chassis/safety_gates.h"

using namespace Chassis;
void setUp() {}
void tearDown() {}

void test_ap_defaults_threshold_and_disabled()
{
    ApFirstGate gate;
    TEST_ASSERT_TRUE(gate.enabled);
    TEST_ASSERT_FALSE(gate.edge);
    TEST_ASSERT_FALSE(gate.minimal);
    for (std::uint8_t raw : {0, 1})
        TEST_ASSERT_FALSE(gate.allows(raw, 2000u, 0u));
    for (std::uint8_t raw : {3, 6, 8, 9, 15})
        TEST_ASSERT_TRUE(gate.allows(raw, 2000u, 0u));
    TEST_ASSERT_FALSE(gate.allows(2u, 2000u, 0u));
    gate.enabled = false;
    TEST_ASSERT_TRUE(gate.allows(0u, 0u, 0u));
}

void test_ap_time_boundary_and_wrap()
{
    ApFirstGate gate;
    for (std::uint32_t start : {123u, 0xFFFFFF00u})
    {
        TEST_ASSERT_FALSE(gate.allows(3u, start, start));
        TEST_ASSERT_FALSE(gate.allows(3u, std::uint32_t(start + 999u), start));
        TEST_ASSERT_TRUE(gate.allows(3u, std::uint32_t(start + 1000u), start));
        TEST_ASSERT_TRUE(gate.allows(3u, std::uint32_t(start + 1001u), start));
    }
}

void test_ap_overrides_only_delay()
{
    for (unsigned mode = 1; mode <= 3; ++mode)
    {
        ApFirstGate gate;
        gate.edge = (mode & 1u) != 0;
        gate.minimal = (mode & 2u) != 0;
        TEST_ASSERT_FALSE(gate.allows(0u, 42u, 42u));
        TEST_ASSERT_FALSE(gate.allows(1u, 42u, 42u));
        TEST_ASSERT_TRUE(gate.allows(3u, 42u, 42u));
    }
}

void test_abort_latch_persistence_clear_and_rearm()
{
    for (std::uint8_t abort : {8, 9})
    {
        for (std::uint8_t clear : {0, 1, 2})
        {
            AbortGuard gate;
            TEST_ASSERT_TRUE(gate.enabled);
            TEST_ASSERT_TRUE(gate.allows());
            gate.update(3u);
            TEST_ASSERT_TRUE(gate.allows());
            gate.update(abort);
            TEST_ASSERT_FALSE(gate.allows());
            for (std::uint8_t raw : {3, 6, 7, 10, 15})
            {
                gate.update(raw);
                TEST_ASSERT_FALSE(gate.allows());
            }
            gate.update(clear);
            TEST_ASSERT_TRUE(gate.allows());
            gate.update(abort);
            TEST_ASSERT_FALSE(gate.allows());
        }
    }
}

void test_abort_disabled_preserves_state()
{
    AbortGuard gate;
    gate.enabled = false;
    gate.update(8u);
    TEST_ASSERT_TRUE(gate.allows());
    gate.enabled = true;
    TEST_ASSERT_TRUE(gate.allows());
    gate.update(9u);
    gate.enabled = false;
    TEST_ASSERT_TRUE(gate.allows());
    gate.update(0u);
    gate.enabled = true;
    TEST_ASSERT_FALSE(gate.allows());
    gate.update(1u);
    TEST_ASSERT_TRUE(gate.allows());
}

void test_soft_missing_invalid_and_boundaries()
{
    SoftEngageGate gate;
    TEST_ASSERT_TRUE(gate.enabled);
    TEST_ASSERT_FALSE(gate.allows({}));
    TEST_ASSERT_FALSE(gate.allows({0.0f, false}));
    for (float angle : {-5.001f, 5.001f,
                        std::numeric_limits<float>::quiet_NaN(),
                        std::numeric_limits<float>::infinity(),
                        -std::numeric_limits<float>::infinity()})
        TEST_ASSERT_FALSE(gate.allows({angle, true}));
    for (float angle : {-5.0f, 5.0f, 0.0f})
    {
        gate.reset();
        TEST_ASSERT_FALSE(gate.allows({angle, false}));
        TEST_ASSERT_TRUE(gate.allows({angle, true}));
    }
}

void test_soft_latch_disabled_and_reset()
{
    SoftEngageGate gate;
    gate.enabled = false;
    TEST_ASSERT_TRUE(gate.allows({}));
    gate.enabled = true;
    TEST_ASSERT_FALSE(gate.allows({})); // disabled evaluation did not latch
    TEST_ASSERT_TRUE(gate.allows({0.0f, true}));
    TEST_ASSERT_TRUE(gate.allows({90.0f, true}));
    TEST_ASSERT_TRUE(gate.allows({}));
    gate.enabled = false;
    TEST_ASSERT_TRUE(gate.allows({}));
    gate.enabled = true;
    TEST_ASSERT_TRUE(gate.allows({})); // toggle preserves latch
    gate.reset();
    TEST_ASSERT_FALSE(gate.allows({}));
    TEST_ASSERT_FALSE(gate.allows({90.0f, true}));
    TEST_ASSERT_TRUE(gate.allows({-5.0f, true}));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_ap_defaults_threshold_and_disabled);
    RUN_TEST(test_ap_time_boundary_and_wrap);
    RUN_TEST(test_ap_overrides_only_delay);
    RUN_TEST(test_abort_latch_persistence_clear_and_rearm);
    RUN_TEST(test_abort_disabled_preserves_state);
    RUN_TEST(test_soft_missing_invalid_and_boundaries);
    RUN_TEST(test_soft_latch_disabled_and_reset);
    return UNITY_END();
}
