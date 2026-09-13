#pragma once

#include <stdint.h>
#include <string.h>

#include "../can_frame_types.h"

namespace CanAnomaly
{

static constexpr uint8_t kTrackedIds = 32;
static constexpr uint32_t kRxStallMs = 2000;
static constexpr uint32_t kNewIdWarmupMs = 5000;
static constexpr uint16_t kBurstFloorMs = 2;

enum Flag : uint16_t
{
    NewId = 1U << 0,
    DlcChanged = 1U << 1,
    PeriodChanged = 1U << 2,
    RateBurst = 1U << 3,
    RxStalled = 1U << 4,
    RxRecovered = 1U << 5,
    BusAsymmetry = 1U << 6,
    TxEchoMismatch = 1U << 7,
    CapacityExceeded = 1U << 8,
};

struct Summary
{
    uint16_t flags = 0;
    uint32_t events = 0;
    uint32_t frames = 0;
    uint32_t newIds = 0;
    uint32_t dlcChanges = 0;
    uint32_t periodChanges = 0;
    uint32_t bursts = 0;
    uint32_t stalls = 0;
    uint32_t recoveries = 0;
    uint32_t asymmetries = 0;
    uint32_t echoMismatches = 0;
    uint32_t capacityDrops = 0;
    uint32_t lastEventMs = 0;
    uint32_t lastId = 0;
    uint8_t lastPhysicalBus = CAN_BUS_ANY;
    bool policyBlock = false;
};

class Tracker
{
public:
    void begin(uint32_t nowMs)
    {
        reset();
        beginMs_ = nowMs;
    }

    void reset()
    {
        memset(entries_, 0, sizeof(entries_));
        summary_ = {};
        beginMs_ = 0;
        lastRxMs_[0] = lastRxMs_[1] = 0;
        seenBus_[0] = seenBus_[1] = false;
        stalled_[0] = stalled_[1] = false;
    }

    void observe(const CanFrame &frame, uint32_t nowMs)
    {
        ++summary_.frames;
        const int busIndex = physicalIndex(frame.physicalBus);
        if (busIndex >= 0)
        {
            if (stalled_[busIndex])
            {
                stalled_[busIndex] = false;
                record(RxRecovered, frame, nowMs);
                ++summary_.recoveries;
            }
            seenBus_[busIndex] = true;
            lastRxMs_[busIndex] = nowMs;
        }

        Entry *entry = find(frame.id, frame.physicalBus);
        if (!entry)
        {
            entry = allocate();
            if (!entry)
            {
                record(CapacityExceeded, frame, nowMs);
                ++summary_.capacityDrops;
                summary_.policyBlock = true;
                return;
            }
            entry->used = true;
            entry->id = frame.id;
            entry->physicalBus = frame.physicalBus;
            entry->dlc = frame.dlc;
            entry->lastMs = nowMs;
            if (static_cast<uint32_t>(nowMs - beginMs_) >= kNewIdWarmupMs)
            {
                record(NewId, frame, nowMs);
                ++summary_.newIds;
            }
            return;
        }

        if (entry->dlc != frame.dlc)
        {
            record(DlcChanged, frame, nowMs);
            ++summary_.dlcChanges;
            summary_.policyBlock = true;
            entry->dlc = frame.dlc;
        }

        const uint32_t period = nowMs - entry->lastMs;
        if (period < kBurstFloorMs)
        {
            record(RateBurst, frame, nowMs);
            ++summary_.bursts;
        }
        if (entry->periodMs != 0)
        {
            const uint32_t high = entry->periodMs * 3U;
            const uint32_t low = entry->periodMs / 3U;
            if (period > high || period < low)
            {
                record(PeriodChanged, frame, nowMs);
                ++summary_.periodChanges;
            }
        }
        if (period >= kBurstFloorMs)
            entry->periodMs = entry->periodMs == 0 ? period :
                              static_cast<uint32_t>((entry->periodMs * 3U + period) / 4U);
        entry->lastMs = nowMs;
    }

    void tick(uint32_t nowMs)
    {
        for (uint8_t i = 0; i < 2; ++i)
        {
            if (seenBus_[i] && !stalled_[i] && nowMs - lastRxMs_[i] > kRxStallMs)
            {
                stalled_[i] = true;
                CanFrame marker;
                marker.physicalBus = i == 0 ? CAN_BUS_CAN_A : CAN_BUS_CAN_B;
                record(RxStalled, marker, nowMs);
                ++summary_.stalls;
                summary_.policyBlock = true;
            }
        }
        if (seenBus_[0] != seenBus_[1] && nowMs - beginMs_ > kRxStallMs)
        {
            if ((summary_.flags & BusAsymmetry) == 0)
            {
                CanFrame marker;
                marker.physicalBus = seenBus_[0] ? CAN_BUS_CAN_A : CAN_BUS_CAN_B;
                record(BusAsymmetry, marker, nowMs);
                ++summary_.asymmetries;
            }
        }
    }

    void noteTxEcho(const CanFrame &tx, const CanFrame *echo, uint32_t nowMs)
    {
        bool mismatch = !echo || tx.id != echo->id || tx.dlc != echo->dlc ||
                        tx.bus != echo->bus;
        if (!mismatch && tx.dlc > 0)
            mismatch = memcmp(tx.data, echo->data, tx.dlc) != 0;
        if (mismatch)
        {
            record(TxEchoMismatch, tx, nowMs);
            ++summary_.echoMismatches;
            summary_.policyBlock = true;
        }
    }

    Summary summary() const { return summary_; }

private:
    struct Entry
    {
        uint32_t id = 0;
        uint32_t lastMs = 0;
        uint32_t periodMs = 0;
        uint8_t physicalBus = CAN_BUS_ANY;
        uint8_t dlc = 0;
        bool used = false;
    };

    static int physicalIndex(uint8_t bus)
    {
        if (bus == CAN_BUS_CAN_A) return 0;
        if (bus == CAN_BUS_CAN_B) return 1;
        return -1;
    }

    Entry *find(uint32_t id, uint8_t physicalBus)
    {
        for (uint8_t i = 0; i < kTrackedIds; ++i)
            if (entries_[i].used && entries_[i].id == id &&
                entries_[i].physicalBus == physicalBus)
                return &entries_[i];
        return nullptr;
    }

    Entry *allocate()
    {
        for (uint8_t i = 0; i < kTrackedIds; ++i)
            if (!entries_[i].used) return &entries_[i];
        return nullptr;
    }

    void record(uint16_t flag, const CanFrame &frame, uint32_t nowMs)
    {
        summary_.flags = static_cast<uint16_t>(summary_.flags | flag);
        ++summary_.events;
        summary_.lastEventMs = nowMs;
        summary_.lastId = frame.id;
        summary_.lastPhysicalBus = frame.physicalBus;
    }

    Entry entries_[kTrackedIds] = {};
    Summary summary_ = {};
    uint32_t beginMs_ = 0;
    uint32_t lastRxMs_[2] = {};
    bool seenBus_[2] = {};
    bool stalled_[2] = {};
};

} // namespace CanAnomaly
