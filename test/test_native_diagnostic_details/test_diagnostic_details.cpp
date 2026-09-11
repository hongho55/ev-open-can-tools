#include <cassert>
#include <cmath>
#include <unity.h>
#include "chassis/telemetry_state.h"
#include "chassis/event_recorder.h"
using namespace Chassis;
void setUp() {}
void tearDown() {}

static CanFrame frame(uint32_t id, uint8_t bus = CAN_BUS_PARTY, uint8_t dlc = 8) {
    CanFrame f; f.id=id; f.bus=bus; f.dlc=dlc; return f;
}
void test_diagnostic_details() {
    TelemetryState t(DasLayout::StandardHw4, 100);
    auto hv=frame(0x132); hv.data[0]=0x40; hv.data[1]=0x9C; // 400 V
    hv.data[2]=0x85; hv.data[3]=0xFF; // -12.3 A
    assert(t.observe(hv, 10));
    auto s=t.snapshot(10);
    assert(s.bmsHvSeen && std::fabs(s.packVoltageV-400)<0.01);
    assert(std::fabs(s.packCurrentA+12.3)<0.01);
    auto soc=frame(0x292); const unsigned raw=755;
    soc.data[1]=(raw & 63)<<2; soc.data[2]=raw>>6;
    assert(t.observe(soc, 109));
    s=t.snapshot(110); assert(!s.bmsHvSeen && s.bmsSocSeen);
    assert(std::fabs(s.socPercent-75.5)<0.01);
    soc.data[1]=255; soc.data[2]=15; assert(!t.observe(soc,111));
    auto thermal=frame(0x312); thermal.data[4]=35; thermal.data[5]=80;
    assert(t.observe(thermal,120)); s=t.snapshot(120);
    assert(s.tempMinC==-5 && s.tempMaxC==40);
    thermal.dlc=5; assert(!t.observe(thermal,121));
    hv.dlc=3; assert(!t.observe(hv,121));
    hv.dlc=8; hv.bus=CAN_BUS_CH; assert(!t.observe(hv,121));
    // HW4 AP state is byte 0; keep byte 1 intentionally different so this
    // fixture catches a decoder that accidentally reads the telemetry nibble.
    auto das=frame(0x39B,CAN_BUS_CH); das.data[0]=0xA3; das.data[1]=0xF0;
    das.data[2]=0x8C; das.data[4]=2; das.data[5]=0x88; das.data[6]=3;
    assert(t.observe(das,200)); s=t.snapshot(200);
    assert(s.apState==3 && s.handsOn==2 && s.laneChange==14);
    assert(s.forwardWarning==2 && s.sideWarning==2 && s.visionLimitKph==60);
    auto unrelated=frame(0x3FD,CAN_BUS_CH); assert(t.observe(unrelated,299));
    s=t.snapshot(300); assert(!s.dasSeen && !s.visionLimitSeen && s.apControlSeen);
    auto wrong=frame(0x399,CAN_BUS_CH); assert(!t.observe(wrong,301));
    t.setLayout(DasLayout::Unknown); assert(!t.snapshot(301).bmsSocSeen);
    t.setLayout(DasLayout::StandardHw4); assert(t.observe(das,0xFFFFFFF0));
    assert(t.snapshot(0x10).dasSeen); assert(!t.snapshot(0x100).dasSeen);
    EventRecorder r; auto f=frame(0x132); r.observe(f,1); assert(r.count()==0);
    r.enable(true);
    assert(r.rawCapacity() == EventRecorder::InternalRawCapacity);
    assert(r.rawHistoryCapacity() == EventRecorder::InternalRawCapacity - EventRecorder::InternalPostRawCapacity);
    assert(r.rawPostCapacity() == EventRecorder::InternalPostRawCapacity);
    assert(r.rawHistoryCapacity() + r.rawPostCapacity() == r.rawCapacity());
    for (unsigned i=0;i<300;++i) r.observe(f,i);
    assert(r.count()==300 && r.rawCount()==300 && r.rawDrops()==0);
    assert(r.mark(EventRecorder::Trigger::Manual,300));
    assert(!r.mark(EventRecorder::Trigger::CanError,301));
    for (unsigned i=300;i<364;++i) r.observe(f,i);
    assert(!r.frozen());
    r.tick(10300); assert(r.frozen() && r.count()==364);
    EventRecorder::Entry e; assert(r.entry(0,e) && e.ms==0);
    assert(r.entry(363,e) && e.ms==363); assert(!r.entry(364,e));
    r.observe(f,10400); assert(r.entry(363,e) && e.ms==363); assert(!r.entry(364,e));
    r.clear(); r.noteAp(3,500); r.noteAp(8,501); r.tick(10501);
    assert(r.frozen());
    r.enable(false); assert(!r.mark(EventRecorder::Trigger::Manual,2600));
    r.enable(true); auto unknown=frame(0x123); r.observe(unknown,1); assert(r.count()==0);
    assert(r.mark(EventRecorder::Trigger::CanError,0xFFFFFFF0)); r.tick(0x2800);
    assert(r.frozen());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_diagnostic_details);
    return UNITY_END();
}
