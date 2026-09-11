#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../can_frame_types.h"

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace Chassis
{
// Observational, bounded flight recorder. CAN-path methods never allocate and
// never make a control decision. Filesystem serialization belongs to the web
// maintenance context (see mcp2515_dashboard.h).
class EventRecorder
{
public:
    // The T-2CAN N16R8 build uses the larger PSRAM rings when the boot probe
    // succeeds. The internal-RAM rings are deliberately useful on their own so
    // a board with absent or failed PSRAM still records an incident.
    static constexpr size_t InternalRawCapacity = 2048;
    static constexpr size_t InternalStateCapacity = 3072;
    static constexpr size_t PsramRawCapacity = 8192;
    static constexpr size_t PsramStateCapacity = 6144;
    static constexpr size_t InternalPostRawCapacity = 1536;
    static constexpr size_t InternalPostStateCapacity = 512;
    static constexpr size_t PsramPostRawCapacity = 4096;
    static constexpr size_t PsramPostStateCapacity = 1024;
    static constexpr size_t Capacity = InternalRawCapacity; // compatibility
    static constexpr size_t MaxStateCapacity = InternalStateCapacity;
    static constexpr uint32_t PostWindowMs = 10000;
    static constexpr uint32_t StateTargetWindowMs = 5 * 60 * 1000;
    static constexpr uint32_t StateHistoryBucketMs = 1000;
    static constexpr size_t InternalStateHistoryBudgetPerBucket = 8;
    static constexpr size_t PsramStateHistoryBudgetPerBucket = 16;
    static constexpr uint32_t TorqueHistoryIntervalMs = 1000;
    static constexpr uint32_t TorquePostIntervalMs = 100;

    static_assert(InternalStateCapacity - InternalPostStateCapacity >=
                      (StateTargetWindowMs / StateHistoryBucketMs + 1) *
                          InternalStateHistoryBudgetPerBucket,
                  "internal state history must protect five minutes");
    static_assert(PsramStateCapacity - PsramPostStateCapacity >=
                      (StateTargetWindowMs / StateHistoryBucketMs + 1) *
                          PsramStateHistoryBudgetPerBucket,
                  "PSRAM state history must protect five minutes");

    static bool postDeadlineReached(uint32_t now, uint32_t triggerMs)
    {
        return uint32_t(now - triggerMs) >= PostWindowMs;
    }

    enum class Trigger : uint8_t { None, Manual, ApAbort, ApDisengage, CanError, CanLoss };
    enum class Direction : uint8_t { Rx, Tx };
    enum class StateKind : uint8_t { ApState, NagMode, Torque, InjectionDecision, Setting, CanHealth, CanLiveness };

    struct Entry { uint32_t ms = 0; CanFrame frame{}; }; // raw RX compatibility
    struct RawRecord
    {
        uint32_t ms = 0;
        CanFrame frame{};
        Direction direction = Direction::Rx;
        bool txOk = true;
        bool txAttempted = true;
    };
    struct StateRecord
    {
        uint32_t ms = 0;
        StateKind kind = StateKind::Setting;
        uint16_t reason = 0;
        uint16_t value = 0;
        uint32_t value32 = 0;
    };
    struct EffectiveSetting
    {
        uint16_t setting = 0;
        uint32_t value = 0;
    };

    EventRecorder()
    {
        setPartitionCapacities();
    }

    // Configure once during setup. A failed/absent PSRAM probe uses a smaller
    // internal fallback and is reported through capacity()/usingPsram().
    bool configure(bool verifiedPsram, size_t psramBytes)
    {
        usingPsram_ = false;
        raw_ = fallbackRaw_;
        state_ = fallbackState_;
        rawCapacity_ = InternalRawCapacity;
        stateCapacity_ = InternalStateCapacity;
        setPartitionCapacities();
#ifdef ESP_PLATFORM
        if (verifiedPsram && psramBytes >= 4096)
        {
            void *raw = heap_caps_malloc(sizeof(RawRecord) * PsramRawCapacity,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            void *states = heap_caps_malloc(sizeof(StateRecord) * PsramStateCapacity,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (raw && states)
            {
                raw_ = static_cast<RawRecord *>(raw);
                state_ = static_cast<StateRecord *>(states);
                usingPsram_ = true;
                rawCapacity_ = PsramRawCapacity;
                stateCapacity_ = PsramStateCapacity;
                setPartitionCapacities();
            }
            else
            {
                if (raw) heap_caps_free(raw);
                if (states) heap_caps_free(states);
            }
        }
#else
        (void)verifiedPsram;
        (void)psramBytes;
#endif
        effectiveCount_ = 0;
        clear();
        configured_ = true;
        return true;
    }

    void enable(bool value) { enabled_ = value; clear(); }
    void clear()
    {
        ++generation_;
        rawHead_ = rawCount_ = stateHead_ = stateCount_ = 0;
        rawDrops_ = stateDrops_ = 0;
        stateProtectedDrops_ = 0;
        postTriggered_ = false;
        trigger_ = Trigger::None;
        frozen_ = false;
        frozenEffectiveCount_ = 0;
        triggerMs_ = 0;
        postRawHead_ = postRawCount_ = postStateHead_ = postStateCount_ = 0;
        lastAp_ = 0xFF;
        lastNag_ = 0xFF;
        lastApAbortState_ = 0;
        lastTorque_ = 0xFFFF;
        lastTorqueMs_ = 0;
        haveTorque_ = false;
        lastDecision_ = 0xFFFF;
        lastApEventState_ = 0;
        haveApEventState_ = false;
        memset(haveCanHealth_, 0, sizeof(haveCanHealth_));
        memset(lastCanHealthFlags_, 0, sizeof(lastCanHealthFlags_));
        memset(lastCanHealthErrors_, 0, sizeof(lastCanHealthErrors_));
        haveCanLiveness_ = false;
        lastCanLiveness_ = 0;
        stateHistoryBucketStartMs_ = 0;
        stateHistoryBucketCount_ = 0;
        stateHistoryBucketActive_ = false;
    }

    void tick(uint32_t now)
    {
        if (trigger_ != Trigger::None && !frozen_ && postDeadlineReached(now, triggerMs_))
        {
            frozenEffectiveCount_ = effectiveCount_;
            memcpy(frozenEffectiveSettings_, effectiveSettings_,
                   sizeof(frozenEffectiveSettings_));
            frozen_ = true;
        }
    }

    void beginConfigurationUpdate()
    {
        if (configurationUpdateDepth_ < UINT16_MAX)
            ++configurationUpdateDepth_;
    }
    void endConfigurationUpdate(uint32_t /*now*/)
    {
        if (configurationUpdateDepth_ != 0)
            --configurationUpdateDepth_;
    }
    bool configurationUpdateActive() const { return configurationUpdateDepth_ != 0; }

    bool mark(Trigger why, uint32_t now)
    {
        tick(now);
        if (!enabled_ || trigger_ != Trigger::None || why == Trigger::None) return false;
        trigger_ = why;
        triggerMs_ = now;
        postTriggered_ = true;
        return true;
    }

    void noteAp(uint8_t ap, uint32_t now)
    {
        recordApState(ap, now);
        const bool abort = ap == 8 || ap == 9;
        const bool previousAbort = lastApAbortState_ == 8 || lastApAbortState_ == 9;
        const bool previousEngaged = haveApEventState_ && lastApEventState_ >= 3 && lastApEventState_ <= 6;
        const bool engaged = ap >= 3 && ap <= 6;
        if (abort && !previousAbort)
            mark(Trigger::ApAbort, now);
        else if (previousEngaged && !engaged)
            mark(Trigger::ApDisengage, now);
        lastApAbortState_ = ap;
        lastApEventState_ = ap;
        haveApEventState_ = true;
    }

    // Existing documented/core RX IDs only. TX is intentionally not filtered.
    void observe(const CanFrame &f, uint32_t now)
    {
        tick(now);
        if (!enabled_ || frozen_ || !keyId(f.id) || f.dlc > 8 ||
            (f.bus == CAN_BUS_ANY && f.physicalBus == CAN_BUS_ANY)) return;
        appendRaw({now, f, Direction::Rx, true});
    }

    void observeTx(const CanFrame &f, bool ok, uint32_t now, bool attempted = true)
    {
        tick(now);
        if (!enabled_ || frozen_ || f.dlc > 8) return;
        appendRaw({now, f, Direction::Tx, ok, attempted});
    }

    void recordApState(uint8_t ap, uint32_t now)
    {
        tick(now);
        if (!enabled_ || frozen_ || ap == lastAp_) return;
        lastAp_ = ap;
        appendState({now, StateKind::ApState, 0, ap, 0});
    }
    void recordNag(uint8_t mode, uint32_t now)
    {
        tick(now);
        if (!enabled_ || frozen_ || mode == lastNag_) return;
        lastNag_ = mode;
        appendState({now, StateKind::NagMode, 0, mode, 0});
    }
    void recordTorque(uint32_t value, uint32_t now)
    {
        tick(now);
        const uint32_t interval = trigger_ == Trigger::None
                                      ? TorqueHistoryIntervalMs
                                      : TorquePostIntervalMs;
        if (!enabled_ || frozen_ || value > 0x1FFFu ||
            (haveTorque_ && (value == lastTorque_ || uint32_t(now - lastTorqueMs_) < interval)))
            return;
        lastTorque_ = static_cast<uint16_t>(value);
        lastTorqueMs_ = now;
        haveTorque_ = true;
        appendState({now, StateKind::Torque, 0, static_cast<uint16_t>(value), value});
    }
    void recordInjectionDecision(bool allowed, uint16_t reason, uint32_t now)
    {
        tick(now);
        const uint16_t packed = static_cast<uint16_t>((allowed ? 0x8000u : 0u) | reason);
        if (!enabled_ || frozen_ || packed == lastDecision_) return;
        lastDecision_ = packed;
        appendState({now, StateKind::InjectionDecision, reason, static_cast<uint16_t>(allowed ? 1 : 0), 0});
    }
    void recordSetting(uint16_t setting, uint32_t value, uint32_t now)
    {
        tick(now);
        rememberSetting(setting, value);
        if (!enabled_ || frozen_) return;
        appendState({now, StateKind::Setting, setting, static_cast<uint16_t>(value), value});
    }
    void recordCanHealth(uint8_t bus, bool ready, uint32_t errors, uint32_t now)
    {
        tick(now);
        if (!enabled_ || frozen_ || bus >= 3)
            return;
        const uint8_t flags = ready ? 1 : 0;
        if (haveCanHealth_[bus] && lastCanHealthFlags_[bus] == flags &&
            lastCanHealthErrors_[bus] == errors)
            return;
        haveCanHealth_[bus] = true;
        lastCanHealthFlags_[bus] = flags;
        lastCanHealthErrors_[bus] = errors;
        appendState({now, StateKind::CanHealth, bus, flags, errors});
    }
    void recordCanLiveness(bool online, uint32_t now)
    {
        tick(now);
        if (!enabled_ || frozen_)
            return;
        const uint16_t value = online ? 1 : 0;
        if (haveCanLiveness_ && lastCanLiveness_ == value)
        {
            return;
        }
        haveCanLiveness_ = true;
        lastCanLiveness_ = value;
        appendState({now, StateKind::CanLiveness, 0, value, 0});
    }

    bool entry(size_t index, Entry &out) const
    {
        RawRecord raw;
        if (!frozen_ || !rawRecord(index, raw) || raw.direction != Direction::Rx) return false;
        out.ms = raw.ms;
        out.frame = raw.frame;
        return true;
    }
    bool rawRecord(size_t index, RawRecord &out) const
    {
        const size_t preCount = rawCount_;
        const size_t total = rawCount();
        if (index >= total) return false;
        if (index < preCount)
        {
            out = raw_[(rawHead_ + rawHistoryCapacity_ - preCount + index) % rawHistoryCapacity_];
            return true;
        }
        if (postRawCapacity_ == 0) return false;
        const size_t postIndex = (postRawHead_ + postRawCapacity_ - postRawCount_ + index - preCount) % postRawCapacity_;
        out = raw_[rawHistoryCapacity_ + postIndex];
        return true;
    }
    bool stateRecord(size_t index, StateRecord &out) const
    {
        const size_t preCount = stateCount_;
        const size_t total = stateCount();
        if (index >= total) return false;
        if (index < preCount)
        {
            out = state_[(stateHead_ + stateHistoryCapacity_ - preCount + index) % stateHistoryCapacity_];
            return true;
        }
        if (postStateCapacity_ == 0) return false;
        const size_t postIndex = (postStateHead_ + postStateCapacity_ - postStateCount_ + index - preCount) % postStateCapacity_;
        out = state_[stateHistoryCapacity_ + postIndex];
        return true;
    }
    size_t copyRaw(RawRecord *out, size_t limit) const
    {
        const size_t n = rawCount() < limit ? rawCount() : limit;
        for (size_t i = 0; i < n; ++i) rawRecord(i, out[i]);
        return n;
    }
    size_t copyState(StateRecord *out, size_t limit) const
    {
        const size_t n = stateCount() < limit ? stateCount() : limit;
        for (size_t i = 0; i < n; ++i) stateRecord(i, out[i]);
        return n;
    }

    bool enabled() const { return enabled_; }
    bool configured() const { return configured_; }
    bool frozen() const { return frozen_; }
    size_t count() const { return rawCount(); }
    size_t rawCount() const { return rawCount_ + postRawCount_; }
    size_t stateCount() const { return stateCount_ + postStateCount_; }
    size_t rawCapacity() const { return rawCapacity_; }
    size_t stateCapacity() const { return stateCapacity_; }
    size_t rawHistoryCapacity() const { return rawHistoryCapacity_; }
    size_t stateHistoryCapacity() const { return stateHistoryCapacity_; }
    size_t rawPostCapacity() const { return postRawCapacity_; }
    size_t statePostCapacity() const { return postStateCapacity_; }
    size_t capacity() const { return rawCapacity_; }
    uint32_t rawDrops() const { return rawDrops_; }
    uint32_t stateDrops() const { return stateDrops_; }
    uint32_t stateProtectedDrops() const { return stateProtectedDrops_; }
    uint32_t drops() const { return rawDrops_ + stateDrops_; }
    bool usingPsram() const { return usingPsram_; }
    uint32_t triggerMs() const { return triggerMs_; }
    uint32_t generation() const { return generation_; }
    size_t stateHistoryBudgetPerBucket() const { return stateHistoryBudgetPerBucket_; }
    size_t effectiveSettingCount() const { return frozen_ ? frozenEffectiveCount_ : effectiveCount_; }
    bool effectiveSetting(size_t index, EffectiveSetting &out) const
    {
        const size_t count = frozen_ ? frozenEffectiveCount_ : effectiveCount_;
        if (index >= count)
            return false;
        out = frozen_ ? frozenEffectiveSettings_[index] : effectiveSettings_[index];
        return true;
    }
    uint32_t coverageMs() const
    {
        const size_t rawTotal = rawCount();
        const size_t stateTotal = stateCount();
        if (rawTotal == 0 && stateTotal == 0) return 0;
        RawRecord rawFirst, rawLast;
        StateRecord stateFirst, stateLast;
        bool haveFirst = false;
        uint32_t first = 0, last = 0;
        if (rawTotal && rawRecord(0, rawFirst) && rawRecord(rawTotal - 1, rawLast))
        {
            first = rawFirst.ms;
            last = rawLast.ms;
            haveFirst = true;
        }
        if (stateTotal && stateRecord(0, stateFirst) && stateRecord(stateTotal - 1, stateLast))
        {
            if (!haveFirst || uint32_t(first - stateFirst.ms) < 0x80000000u) first = stateFirst.ms;
            if (!haveFirst || uint32_t(stateLast.ms - last) < 0x80000000u) last = stateLast.ms;
        }
        return last - first;
    }
    uint32_t stateHistoryCoverageMs() const
    {
        if (stateCount_ < 2 || stateHistoryCapacity_ == 0) return 0;
        const size_t firstIndex =
            (stateHead_ + stateHistoryCapacity_ - stateCount_) % stateHistoryCapacity_;
        const size_t lastIndex =
            (stateHead_ + stateHistoryCapacity_ - 1) % stateHistoryCapacity_;
        return state_[lastIndex].ms - state_[firstIndex].ms;
    }
    bool stateTargetWindowReady() const
    {
        return stateHistoryCoverageMs() >= StateTargetWindowMs;
    }
    const char *reason() const
    {
        switch (trigger_) {
        case Trigger::Manual: return "manual";
        case Trigger::ApAbort: return "ap_abort";
        case Trigger::ApDisengage: return "ap_disengage";
        case Trigger::CanError: return "can_error";
        case Trigger::CanLoss: return "can_loss";
        default: return "none";
        }
    }
    Trigger trigger() const { return trigger_; }

private:
    static constexpr size_t MaxEffectiveSettings = 32;
    void rememberSetting(uint16_t setting, uint32_t value)
    {
        for (size_t i = 0; i < effectiveCount_; ++i)
        {
            if (effectiveSettings_[i].setting == setting)
            {
                effectiveSettings_[i].value = value;
                return;
            }
        }
        if (effectiveCount_ < MaxEffectiveSettings)
            effectiveSettings_[effectiveCount_++] = {setting, value};
    }
    void setPartitionCapacities()
    {
        postRawCapacity_ = usingPsram_ ? PsramPostRawCapacity : InternalPostRawCapacity;
        postStateCapacity_ = usingPsram_ ? PsramPostStateCapacity : InternalPostStateCapacity;
        rawHistoryCapacity_ = rawCapacity_ - postRawCapacity_;
        stateHistoryCapacity_ = stateCapacity_ - postStateCapacity_;
        stateHistoryBudgetPerBucket_ = usingPsram_ ? PsramStateHistoryBudgetPerBucket
                                                   : InternalStateHistoryBudgetPerBucket;
    }
    static bool keyId(uint32_t id)
    {
        switch (id) {
        case 0x108: case 0x118: case 0x129: case 0x132: case 0x145: case 0x238:
        case 0x257: case 0x292: case 0x2B9: case 0x312: case 0x370:
        case 0x389: case 0x399: case 0x39B: case 0x3EE: case 0x3FD:
        case 0x488: case 0x7FF: return true;
        default: return false;
        }
    }
    void appendRaw(const RawRecord &value)
    {
        if (trigger_ == Trigger::None)
        {
            if (rawCount_ == rawHistoryCapacity_) ++rawDrops_;
            raw_[rawHead_] = value;
            rawHead_ = (rawHead_ + 1) % rawHistoryCapacity_;
            if (rawCount_ < rawHistoryCapacity_) ++rawCount_;
            return;
        }
        if (postRawCapacity_ == 0)
        {
            ++rawDrops_;
            return;
        }
        if (postRawCount_ == postRawCapacity_) ++rawDrops_;
        raw_[rawHistoryCapacity_ + postRawHead_] = value;
        postRawHead_ = (postRawHead_ + 1) % postRawCapacity_;
        if (postRawCount_ < postRawCapacity_) ++postRawCount_;
    }
    void appendState(const StateRecord &value)
    {
        if (trigger_ == Trigger::None)
        {
            if (!stateHistoryBucketActive_ ||
                uint32_t(value.ms - stateHistoryBucketStartMs_) >= StateHistoryBucketMs)
            {
                stateHistoryBucketStartMs_ = value.ms;
                stateHistoryBucketCount_ = 0;
                stateHistoryBucketActive_ = true;
            }
            if (stateHistoryBucketCount_ >= stateHistoryBudgetPerBucket_)
            {
                ++stateDrops_;
                ++stateProtectedDrops_;
                return;
            }
            if (stateCount_ == stateHistoryCapacity_)
            {
                const StateRecord &oldest = state_[stateHead_];
                if (uint32_t(value.ms - oldest.ms) < StateTargetWindowMs)
                {
                    ++stateDrops_;
                    ++stateProtectedDrops_;
                    return;
                }
                ++stateDrops_;
            }
            state_[stateHead_] = value;
            stateHead_ = (stateHead_ + 1) % stateHistoryCapacity_;
            if (stateCount_ < stateHistoryCapacity_) ++stateCount_;
            ++stateHistoryBucketCount_;
            return;
        }
        if (postStateCapacity_ == 0)
        {
            ++stateDrops_;
            return;
        }
        if (postStateCount_ == postStateCapacity_) ++stateDrops_;
        state_[stateHistoryCapacity_ + postStateHead_] = value;
        postStateHead_ = (postStateHead_ + 1) % postStateCapacity_;
        if (postStateCount_ < postStateCapacity_) ++postStateCount_;
    }

    RawRecord *raw_ = fallbackRaw_;
    StateRecord *state_ = fallbackState_;
    RawRecord fallbackRaw_[InternalRawCapacity]{};
    StateRecord fallbackState_[InternalStateCapacity]{};
    size_t rawCapacity_ = InternalRawCapacity;
    size_t stateCapacity_ = InternalStateCapacity;
    size_t rawHistoryCapacity_ = 512;
    size_t stateHistoryCapacity_ = 512;
    size_t postRawCapacity_ = InternalPostRawCapacity;
    size_t postStateCapacity_ = InternalPostStateCapacity;
    size_t stateHistoryBudgetPerBucket_ = InternalStateHistoryBudgetPerBucket;
    size_t rawHead_ = 0, rawCount_ = 0, stateHead_ = 0, stateCount_ = 0;
    size_t postRawHead_ = 0, postRawCount_ = 0, postStateHead_ = 0, postStateCount_ = 0;
    uint32_t rawDrops_ = 0, stateDrops_ = 0;
    uint32_t stateProtectedDrops_ = 0;
    uint32_t triggerMs_ = 0;
    uint32_t generation_ = 0;
    uint32_t stateHistoryBucketStartMs_ = 0;
    size_t stateHistoryBucketCount_ = 0;
    bool stateHistoryBucketActive_ = false;
    EffectiveSetting effectiveSettings_[MaxEffectiveSettings]{};
    EffectiveSetting frozenEffectiveSettings_[MaxEffectiveSettings]{};
    size_t effectiveCount_ = 0;
    size_t frozenEffectiveCount_ = 0;
    uint8_t lastAp_ = 0xFF, lastNag_ = 0xFF, lastApAbortState_ = 0;
    uint8_t lastApEventState_ = 0;
    bool haveApEventState_ = false;
    uint16_t lastTorque_ = 0xFFFF, lastDecision_ = 0xFFFF;
    uint32_t lastTorqueMs_ = 0;
    bool haveTorque_ = false;
    bool haveCanHealth_[3] = {};
    uint8_t lastCanHealthFlags_[3] = {};
    uint32_t lastCanHealthErrors_[3] = {};
    bool haveCanLiveness_ = false;
    uint16_t lastCanLiveness_ = 0;
    uint16_t configurationUpdateDepth_ = 0;
    bool enabled_ = false, configured_ = false, usingPsram_ = false;
    bool postTriggered_ = false, frozen_ = false;
    Trigger trigger_ = Trigger::None;
};
} // namespace Chassis
