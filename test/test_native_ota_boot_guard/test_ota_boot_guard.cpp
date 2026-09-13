#include <unity.h>
#include "ota_boot_guard.h"

static void test_normal_boot_never_requests_an_action()
{
    OtaBootGuard guard;
    guard.begin(false, 100);
    TEST_ASSERT_EQUAL(OtaBootGuard::Action::None, guard.evaluate(false, false, 200));
    TEST_ASSERT_FALSE(guard.txInhibited());
    TEST_ASSERT_EQUAL_STRING("normal", OtaBootGuard::stateName(guard.state()));
}

static void test_pending_boot_inhibits_tx_until_confirmation()
{
    OtaBootGuard guard;
    guard.begin(true, 100);
    TEST_ASSERT_TRUE(guard.txInhibited());
    TEST_ASSERT_EQUAL(OtaBootGuard::Action::Confirm, guard.evaluate(true, true, 101));
    guard.markConfirmed();
    TEST_ASSERT_FALSE(guard.txInhibited());
    TEST_ASSERT_EQUAL_STRING("confirmed", OtaBootGuard::stateName(guard.state()));
}

static void test_failed_preflight_requests_immediate_rollback()
{
    OtaBootGuard guard;
    guard.begin(true, 100);
    TEST_ASSERT_EQUAL(OtaBootGuard::Action::Rollback, guard.evaluate(true, false, 101));
    guard.markRollback();
    TEST_ASSERT_TRUE(guard.txInhibited());
    TEST_ASSERT_EQUAL_STRING("rollback", OtaBootGuard::stateName(guard.state()));
}

static void test_incomplete_preflight_times_out()
{
    OtaBootGuard guard;
    guard.begin(true, 100);
    TEST_ASSERT_EQUAL(OtaBootGuard::Action::None,
                      guard.evaluate(false, false, 100 + OtaBootGuard::kConfirmDeadlineMs - 1));
    TEST_ASSERT_EQUAL_UINT32(1, guard.remainingMs(100 + OtaBootGuard::kConfirmDeadlineMs - 1));
    TEST_ASSERT_EQUAL(OtaBootGuard::Action::Rollback,
                      guard.evaluate(false, false, 100 + OtaBootGuard::kConfirmDeadlineMs));
}

static void test_deadline_handles_millis_wraparound()
{
    OtaBootGuard guard;
    const uint32_t start = UINT32_MAX - 1000;
    guard.begin(true, start);
    TEST_ASSERT_EQUAL(OtaBootGuard::Action::None, guard.evaluate(false, false, start + 500));
    TEST_ASSERT_EQUAL(OtaBootGuard::Action::Rollback,
                      guard.evaluate(false, false, start + OtaBootGuard::kConfirmDeadlineMs));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_normal_boot_never_requests_an_action);
    RUN_TEST(test_pending_boot_inhibits_tx_until_confirmation);
    RUN_TEST(test_failed_preflight_requests_immediate_rollback);
    RUN_TEST(test_incomplete_preflight_times_out);
    RUN_TEST(test_deadline_handles_millis_wraparound);
    return UNITY_END();
}
