#include <assert.h>

#include "diagnostics/can_anomaly_tracker.h"

using namespace CanAnomaly;

static CanFrame frame(uint32_t id, uint8_t dlc, uint8_t bus, uint8_t value = 0)
{
    CanFrame out;
    out.id = id;
    out.dlc = dlc;
    out.bus = CAN_BUS_CH;
    out.physicalBus = bus;
    out.data[0] = value;
    return out;
}

int main()
{
    Tracker tracker;
    tracker.begin(100);
    tracker.observe(frame(0x100, 8, CAN_BUS_CAN_A), 200);
    tracker.observe(frame(0x100, 8, CAN_BUS_CAN_A), 300);
    tracker.observe(frame(0x100, 7, CAN_BUS_CAN_A), 400);
    assert(tracker.summary().dlcChanges == 1);
    assert(tracker.summary().policyBlock);

    tracker.observe(frame(0x100, 7, CAN_BUS_CAN_A), 401);
    assert(tracker.summary().bursts == 1);
    tracker.observe(frame(0x100, 7, CAN_BUS_CAN_A), 900);
    assert(tracker.summary().periodChanges >= 1);

    tracker.tick(3001);
    assert(tracker.summary().stalls == 1);
    assert(tracker.summary().asymmetries == 1);
    tracker.tick(4000);
    assert(tracker.summary().stalls == 1);
    assert(tracker.summary().asymmetries == 1);
    tracker.observe(frame(0x100, 7, CAN_BUS_CAN_A), 4100);
    assert(tracker.summary().recoveries == 1);

    tracker.observe(frame(0x200, 8, CAN_BUS_CAN_B), 6000);
    tracker.observe(frame(0x333, 8, CAN_BUS_CAN_B), 6001);
    assert(tracker.summary().newIds >= 1);

    CanFrame tx = frame(0x370, 8, CAN_BUS_CAN_A, 1);
    CanFrame echo = tx;
    tracker.noteTxEcho(tx, &echo, 6100);
    assert(tracker.summary().echoMismatches == 0);
    echo.data[0] = 2;
    tracker.noteTxEcho(tx, &echo, 6200);
    assert(tracker.summary().echoMismatches == 1);
    tracker.noteTxEcho(tx, nullptr, 6300);
    assert(tracker.summary().echoMismatches == 2);

    Tracker bounded;
    bounded.begin(0);
    for (uint32_t id = 0; id < kTrackedIds; ++id)
        bounded.observe(frame(id, 8, CAN_BUS_CAN_A), id + 1);
    bounded.observe(frame(0x777, 8, CAN_BUS_CAN_A), 100);
    assert(bounded.summary().capacityDrops == 1);
    assert(bounded.summary().policyBlock);

    // No payload is exposed by Summary; only bounded metadata and counters.
    Summary summary = bounded.summary();
    assert(summary.lastId == 0x777);
    assert(summary.lastPhysicalBus == CAN_BUS_CAN_A);
    return 0;
}
