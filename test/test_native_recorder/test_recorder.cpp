#include <cassert>
#include <string>
#include <unity.h>
#include "chassis/event_recorder.h"

using namespace Chassis;

void setUp() {}
void tearDown() {}

static CanFrame frame(uint32_t id, uint8_t bus, uint8_t dlc = 8)
{
    CanFrame f{}; f.id = id; f.bus = bus; f.dlc = dlc; f.data[0] = 0xA5; return f;
}

static void test_raw_rx_tx_bus_result()
{
    EventRecorder r;
    r.enable(true);
    r.observe(frame(0x132, CAN_BUS_PARTY), 10);
    r.observe(frame(0x123, CAN_BUS_PARTY), 11); // not a documented RX key
    auto tx = frame(0x555, CAN_BUS_CH, 2);
    tx.physicalBus = CAN_BUS_CAN_B;
    tx.data[1] = 0x5A;
    r.observeTx(tx, false, 12);
    r.observeTx(frame(0x556, CAN_BUS_CAN_A | CAN_BUS_CAN_B, 2), false, 13, false);
    EventRecorder::RawRecord raw;
    assert(r.rawCount() == 3);
    assert(r.rawRecord(0, raw) && raw.direction == EventRecorder::Direction::Rx && raw.frame.bus == CAN_BUS_PARTY);
    assert(r.rawRecord(1, raw) && raw.direction == EventRecorder::Direction::Tx && !raw.txOk &&
           raw.frame.bus == CAN_BUS_CH && raw.frame.physicalBus == CAN_BUS_CAN_B);
    assert(r.rawRecord(2, raw) && raw.direction == EventRecorder::Direction::Tx && !raw.txOk && !raw.txAttempted);

    auto directRx = frame(0x132, CAN_BUS_ANY);
    directRx.physicalBus = CAN_BUS_CAN_B;
    r.observe(directRx, 14);
    assert(r.rawRecord(3, raw) && raw.direction == EventRecorder::Direction::Rx &&
           raw.frame.bus == CAN_BUS_ANY && raw.frame.physicalBus == CAN_BUS_CAN_B);
}

static void test_state_schema_and_reasons()
{
    EventRecorder r;
    r.enable(true);
    r.recordApState(3, 1);
    r.recordApState(4, 2);
    r.recordNag(2, 3);
    r.recordTorque(0x1234, 4);
    r.recordInjectionDecision(false, 7, 5);
    r.recordSetting(42, 0xBEEF, 6);
    assert(r.stateCount() == 6);
    EventRecorder::StateRecord s;
    assert(r.stateRecord(4, s) && s.kind == EventRecorder::StateKind::InjectionDecision && s.reason == 7 && s.value == 0);
    assert(r.stateRecord(5, s) && s.kind == EventRecorder::StateKind::Setting && s.reason == 42 && s.value32 == 0xBEEF);
    r.observe(frame(0x132, CAN_BUS_PARTY), 20);
    assert(r.coverageMs() == 19);
}

static void test_ring_drop_coverage_and_monotonic_freeze()
{
    EventRecorder r;
    r.enable(true);
    for (uint32_t i = 0; i < 600; ++i) r.observe(frame(0x132, CAN_BUS_PARTY), i);
    assert(r.rawCount() == r.rawHistoryCapacity() && r.rawDrops() == 88);
    assert(r.coverageMs() == 511);
    assert(r.mark(EventRecorder::Trigger::Manual, 600));
    for (uint32_t i = 600; i < 600 + r.rawPostCapacity() + 36; ++i)
        r.observe(frame(0x132, CAN_BUS_PARTY), i);
    assert(r.rawCount() == EventRecorder::Capacity);
    assert(r.rawDrops() == 124);
    EventRecorder::RawRecord raw;
    assert(r.rawRecord(0, raw) && raw.ms == 88);
    assert(r.rawRecord(r.rawHistoryCapacity() - 1, raw) && raw.ms == 599);
    assert(r.rawRecord(r.rawHistoryCapacity(), raw) && raw.ms == 636);
    r.tick(10599);
    assert(!r.frozen()); // no frame-count-only post window
    r.tick(10600);
    assert(r.frozen() && r.reason()[0] == 'm');
    EventRecorder::Entry e;
    assert(r.entry(0, e) && e.ms == 88);
}

static void test_state_deadline_is_enforced()
{
    EventRecorder r;
    r.enable(true);
    assert(EventRecorder::postDeadlineReached(100 + EventRecorder::PostWindowMs, 100));
    assert(!EventRecorder::postDeadlineReached(100 + EventRecorder::PostWindowMs - 1, 100));
    assert(r.mark(EventRecorder::Trigger::Manual, 100));
    r.recordSetting(7, 1, 100 + EventRecorder::PostWindowMs - 1);
    const size_t before = r.stateCount();
    r.recordSetting(7, 2, 100 + EventRecorder::PostWindowMs);
    assert(r.frozen() && r.stateCount() == before);

    EventRecorder second;
    second.enable(true);
    assert(second.mark(EventRecorder::Trigger::Manual, 100));
    assert(!second.mark(EventRecorder::Trigger::CanError, 100 + EventRecorder::PostWindowMs));
    assert(second.frozen());
}

static void test_configuration_update_keeps_exact_deadline_and_balances_depth()
{
    EventRecorder r;
    r.enable(true);
    r.recordSetting(9, 0x44, 10);
    assert(r.mark(EventRecorder::Trigger::Manual, 100));
    r.beginConfigurationUpdate();
    r.beginConfigurationUpdate();
    assert(r.configurationUpdateActive());
    r.tick(100 + EventRecorder::PostWindowMs);
    assert(r.frozen());
    r.recordSetting(9, 0x55, 100 + EventRecorder::PostWindowMs);
    assert(r.frozen());
    r.endConfigurationUpdate(100 + EventRecorder::PostWindowMs);
    assert(r.configurationUpdateActive());
    r.endConfigurationUpdate(100 + EventRecorder::PostWindowMs);
    assert(!r.configurationUpdateActive());
    EventRecorder::EffectiveSetting setting;
    assert(r.effectiveSetting(0, setting) && setting.value == 0x44);
}

static void test_ap_abort_and_fallback()
{
    EventRecorder r;
    assert(r.configure(false, 0));
    assert(!r.usingPsram());
    assert(r.rawCapacity() == EventRecorder::InternalRawCapacity);
    assert(r.stateCapacity() == EventRecorder::InternalStateCapacity);
    r.enable(true);
    r.noteAp(3, 100);
    r.noteAp(8, 101);
    assert(r.reason() == std::string("ap_abort"));
}

static void test_ap_disengage_and_torque_sampling()
{
    EventRecorder ap;
    ap.enable(true);
    ap.noteAp(2, 1);
    ap.noteAp(3, 2);
    ap.noteAp(4, 3);
    ap.noteAp(2, 4);
    assert(ap.reason() == std::string("ap_disengage"));

    EventRecorder torque;
    torque.enable(true);
    torque.recordTorque(0x0100, 100);
    torque.recordTorque(0x0101, 500);  // history is limited to 1 Hz
    torque.recordTorque(0x0102, 1100);
    assert(torque.stateCount() == 2);
    assert(torque.mark(EventRecorder::Trigger::Manual, 1200));
    torque.recordTorque(0x0103, 1250);
    torque.recordTorque(0x0104, 1350); // post-event sampling is 10 Hz
    torque.recordTorque(0x2000, 1500); // invalid 13-bit torque
    assert(torque.stateCount() == 4);
}

static void test_effective_settings_survive_rearm()
{
    EventRecorder r;
    r.enable(true);
    r.recordSetting(9, 0x12345678, 10);
    r.clear();
    assert(r.effectiveSettingCount() == 1);
    EventRecorder::EffectiveSetting setting;
    assert(r.effectiveSetting(0, setting) && setting.setting == 9 && setting.value == 0x12345678);
}

static void test_can_health_changes_are_recorded()
{
    EventRecorder r;
    r.enable(true);
    r.recordCanHealth(1, false, 3, 10);
    r.recordCanHealth(1, false, 3, 11);
    r.recordCanHealth(1, true, 4, 12);
    assert(r.stateCount() == 2);
    EventRecorder::StateRecord state;
    assert(r.stateRecord(1, state) && state.kind == EventRecorder::StateKind::CanHealth &&
           state.reason == 1 && state.value == 1 && state.value32 == 4);
}

static void test_effective_settings_freeze_and_rearm_live_state()
{
    EventRecorder r;
    r.enable(true);
    r.recordSetting(9, 1, 10);
    assert(r.mark(EventRecorder::Trigger::Manual, 100));
    r.recordSetting(9, 2, 5000);
    r.tick(10100);
    r.recordSetting(9, 3, 11000);
    EventRecorder::EffectiveSetting setting;
    assert(r.effectiveSetting(0, setting) && setting.value == 2);
    r.clear();
    assert(r.effectiveSetting(0, setting) && setting.value == 3);
}

static void test_can_liveness_survives_another_trigger()
{
    EventRecorder r;
    r.enable(true);
    r.recordCanLiveness(true, 10);
    assert(r.mark(EventRecorder::Trigger::Manual, 100));
    r.recordCanLiveness(false, 5000);
    r.recordCanLiveness(true, 9000);
    assert(r.stateCount() == 3);
    EventRecorder::StateRecord state;
    assert(r.stateRecord(1, state) && state.kind == EventRecorder::StateKind::CanLiveness && state.value == 0);
    assert(r.stateRecord(2, state) && state.kind == EventRecorder::StateKind::CanLiveness && state.value == 1);
}

static void test_state_history_protects_five_minute_window()
{
    EventRecorder r;
    r.enable(true);
    const size_t budget = r.stateHistoryBudgetPerBucket();
    for (size_t i = 0; i < budget + 4; ++i)
        r.recordSetting(1, static_cast<uint32_t>(i), 10);
    assert(r.stateCount() == budget);
    assert(r.stateProtectedDrops() == 4);

    r.clear();
    for (uint32_t second = 0; second <= 330; ++second)
    {
        for (size_t i = 0; i < budget; ++i)
            r.recordSetting(static_cast<uint16_t>(i), second, second * 1000);
    }
    assert(r.stateTargetWindowReady());
    assert(r.stateHistoryCoverageMs() >= EventRecorder::StateTargetWindowMs);
    assert(r.stateProtectedDrops() == 0);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_raw_rx_tx_bus_result);
    RUN_TEST(test_state_schema_and_reasons);
    RUN_TEST(test_ring_drop_coverage_and_monotonic_freeze);
    RUN_TEST(test_state_deadline_is_enforced);
    RUN_TEST(test_configuration_update_keeps_exact_deadline_and_balances_depth);
    RUN_TEST(test_ap_abort_and_fallback);
    RUN_TEST(test_ap_disengage_and_torque_sampling);
    RUN_TEST(test_effective_settings_survive_rearm);
    RUN_TEST(test_can_health_changes_are_recorded);
    RUN_TEST(test_effective_settings_freeze_and_rearm_live_state);
    RUN_TEST(test_can_liveness_survives_another_trigger);
    RUN_TEST(test_state_history_protects_five_minute_window);
    return UNITY_END();
}
