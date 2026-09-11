#pragma once
#include <cstddef>
#include <cstdint>
#include "../can_frame_types.h"

namespace Chassis {
// Bounded RX-only incident capture. Callers serialize access. No allocation,
// filesystem, retransmission, or vehicle-control decisions live here.
class EventRecorder {
public:
    static constexpr size_t Capacity = 256;
    static constexpr size_t PostFrames = 64;
    struct Entry { uint32_t ms = 0; CanFrame frame{}; };
    enum class Trigger : uint8_t { None, Manual, ApAbort, CanError };
    void enable(bool value) { enabled_ = value; clear(); }
    void clear() {
        head_ = count_ = post_ = 0; trigger_ = Trigger::None;
        frozen_ = false; triggerMs_ = 0; lastAp_ = 0;
    }
    void tick(uint32_t now) {
        if (trigger_ != Trigger::None && !frozen_ && uint32_t(now - triggerMs_) >= 2000)
            frozen_ = true;
    }
    bool mark(Trigger why, uint32_t now) {
        if (!enabled_ || trigger_ != Trigger::None || why == Trigger::None) return false;
        trigger_ = why; triggerMs_ = now; return true;
    }
    void noteAp(uint8_t ap, uint32_t now) {
        if ((ap == 8 || ap == 9) && lastAp_ != 8 && lastAp_ != 9)
            mark(Trigger::ApAbort, now);
        lastAp_ = ap;
    }
    void observe(const CanFrame &f, uint32_t now) {
        tick(now);
        if (!enabled_ || frozen_ || !keyId(f.id) || f.dlc > 8 || f.bus == CAN_BUS_ANY) return;
        entries_[head_] = {now, f}; head_ = (head_ + 1) % Capacity;
        const size_t limit = trigger_ == Trigger::None ? Capacity - PostFrames : Capacity;
        if (count_ < limit) ++count_;
        if (trigger_ != Trigger::None && ++post_ >= PostFrames) frozen_ = true;
    }
    bool entry(size_t index, Entry &out) const {
        if (!frozen_ || index >= count_) return false;
        out = entries_[(head_ + Capacity - count_ + index) % Capacity]; return true;
    }
    bool enabled() const { return enabled_; }
    bool frozen() const { return frozen_; }
    size_t count() const { return count_; }
    uint32_t triggerMs() const { return triggerMs_; }
    const char *reason() const {
        switch (trigger_) {
        case Trigger::Manual: return "manual";
        case Trigger::ApAbort: return "ap_abort";
        case Trigger::CanError: return "can_error";
        default: return "none";
        }
    }
private:
    static bool keyId(uint32_t id) {
        switch (id) {
        case 0x118: case 0x129: case 0x132: case 0x145: case 0x238:
        case 0x257: case 0x292: case 0x2B9: case 0x312: case 0x370:
        case 0x389: case 0x399: case 0x39B: case 0x3EE: case 0x3FD:
        case 0x488: case 0x7FF: return true;
        default: return false;
        }
    }
    Entry entries_[Capacity]{};
    size_t head_ = 0, count_ = 0, post_ = 0;
    bool enabled_ = false, frozen_ = false;
    Trigger trigger_ = Trigger::None;
    uint32_t triggerMs_ = 0;
    uint8_t lastAp_ = 0;
};
}
