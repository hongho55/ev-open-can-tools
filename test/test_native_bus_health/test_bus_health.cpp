#include <unity.h>
#include "drivers/can_bus_health.h"

static void test_requires_fresh_rx_before_tx()
{
    CanBusHealth health;
    health.reset(true);
    TEST_ASSERT_FALSE(health.txAllowed(100));
    TEST_ASSERT_EQUAL_STRING("starting", CanBusHealth::stateName(health.state()));

    health.noteRx(100);
    TEST_ASSERT_TRUE(health.txAllowed(200));
    TEST_ASSERT_EQUAL_STRING("healthy", CanBusHealth::stateName(health.state()));
}

static void test_rx_stall_quarantines_once()
{
    CanBusHealth health;
    health.reset(true);
    health.noteRx(100);
    TEST_ASSERT_FALSE(health.txAllowed(100 + CanBusHealth::kRxStallMs + 1));
    TEST_ASSERT_TRUE(health.quarantined());
    TEST_ASSERT_EQUAL_UINT32(1, health.stallCount());
    TEST_ASSERT_EQUAL_UINT32(1, health.quarantineCount());
    TEST_ASSERT_FALSE(health.txAllowed(5000));
    TEST_ASSERT_EQUAL_UINT32(1, health.stallCount());
}

static void test_controller_recovery_stays_blocked_until_new_rx()
{
    CanBusHealth health;
    health.reset(true);
    health.noteRx(100);
    health.observeController(false);
    TEST_ASSERT_FALSE(health.txAllowed(101));
    TEST_ASSERT_EQUAL_STRING("bus_error", CanBusHealth::stateName(health.state()));

    health.observeController(true);
    TEST_ASSERT_FALSE(health.txAllowed(102));
    TEST_ASSERT_EQUAL_STRING("recovered", CanBusHealth::stateName(health.state()));
    TEST_ASSERT_EQUAL_UINT32(1, health.recoveryCount());

    health.noteRx(103);
    TEST_ASSERT_TRUE(health.txAllowed(104));
}

static void test_three_tx_failures_quarantine_only_that_tracker()
{
    CanBusHealth canA;
    CanBusHealth canB;
    canA.reset(true);
    canB.reset(true);
    canA.noteRx(100);
    canB.noteRx(100);

    canA.noteTx(false, 110);
    canA.noteTx(false, 120);
    TEST_ASSERT_TRUE(canA.txAllowed(121));
    canA.noteTx(false, 130);

    TEST_ASSERT_FALSE(canA.txAllowed(131));
    TEST_ASSERT_TRUE(canB.txAllowed(131));
    TEST_ASSERT_EQUAL_UINT32(3, canA.txFailureCount());
    TEST_ASSERT_EQUAL_UINT32(0, canB.txFailureCount());
}

static void test_bus_fault_event_blocks_while_controller_still_reports_ready()
{
    CanBusHealth health;
    health.reset(true);
    health.noteRx(100);
    TEST_ASSERT_TRUE(health.txAllowed(101));

    health.noteControllerFault();
    health.observeController(true);
    TEST_ASSERT_FALSE(health.txAllowed(102));
    TEST_ASSERT_EQUAL_STRING("bus_error", CanBusHealth::stateName(health.state()));

    health.noteRx(103);
    TEST_ASSERT_TRUE(health.txAllowed(104));
    TEST_ASSERT_EQUAL_UINT32(1, health.recoveryCount());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_requires_fresh_rx_before_tx);
    RUN_TEST(test_rx_stall_quarantines_once);
    RUN_TEST(test_controller_recovery_stays_blocked_until_new_rx);
    RUN_TEST(test_three_tx_failures_quarantine_only_that_tracker);
    RUN_TEST(test_bus_fault_event_blocks_while_controller_still_reports_ready);
    return UNITY_END();
}
