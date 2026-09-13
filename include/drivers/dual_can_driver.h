#pragma once

#include <cstdio>
#include <cstring>

#include "../can_frame_types.h"
#include "can_driver.h"
#include "can_bus_health.h"
#include "dual_can_routing.h"
#include "esp32_mcp2515_driver.h"
#include "twai_driver.h"

// Minimal adapter for the LILYGO T-2CAN: CAN A is the onboard MCP2515 and
// CAN B is the ESP32-S3 native TWAI controller. The wrapper owns both children,
// but never bridges or forwards received frames automatically.
class DualCanDriver : public CanDriver
{
public:
    static constexpr bool kSupportsISR = false;

    DualCanDriver(uint8_t canACsPin, gpio_num_t canBTxPin, gpio_num_t canBRxPin)
        : canA_(canACsPin), canB_(canBTxPin, canBRxPin)
    {
        // Aggregate TX observation belongs to this wrapper. Child callbacks
        // stay empty so one physical send cannot produce duplicate observers.
        canA_.onSendFrame = nullptr;
        canB_.onSendFrame = nullptr;
        canA_.allowSendFrame = nullptr;
        canB_.allowSendFrame = nullptr;
    }

    bool init() override
    {
        // Initialize independently: one absent/faulted transceiver must not
        // take the other physical bus offline.
        const bool canAReady = canA_.init();
        const bool canBReady = canB_.init();
        canAHealth_.reset(canAReady);
        canBHealth_.reset(canBReady);
        canBFaultEpoch_ = canB_.faultEpoch();
        return canAReady || canBReady;
    }

    void setFilters(const uint32_t *ids, uint8_t count) override
    {
        canA_.setFilters(ids, count);
        canB_.setFilters(ids, count);
    }

    bool enableInterrupt(void (* /*onReady*/)()) override { return false; }

    bool ready() const override
    {
        return canA_.ready() || canB_.ready();
    }

    void setMonitorAll(bool enabled) override
    {
        canA_.setMonitorAll(enabled);
        canB_.setMonitorAll(enabled);
    }

    void clearPendingTransmit() override
    {
        canA_.clearPendingTransmit();
        canB_.clearPendingTransmit();
    }

    void selfTestJson(char *out, size_t outLen) override
    {
        if (!out || outLen == 0)
            return;
        char canAResult[192] = {};
        char canBResult[224] = {};
        canA_.selfTestJson(canAResult, sizeof(canAResult));
        canB_.selfTestJson(canBResult, sizeof(canBResult));
        canAHealth_.reset(canA_.ready());
        canBHealth_.reset(canB_.ready());
        snprintf(out, outLen,
                 "{\"type\":\"t2can_self_test_v1\",\"physicalTx\":false,"
                 "\"canA\":%s,\"canB\":%s}",
                 canAResult, canBResult);
    }

    void setSimLoopback(bool enabled) override
    {
        canA_.setSimLoopback(enabled);
        canB_.setSimLoopback(enabled);
    }

    bool read(CanFrame &frame) override
    {
        syncCanBFault();
        canAHealth_.observeController(canA_.ready());
        canBHealth_.observeController(canB_.ready());
        CanFrame candidate;
        if (nextReadA_)
        {
            if (readFrom(canA_, DualCanRouting::canABusLabel(), CAN_BUS_CAN_A, candidate))
            {
                canARxCount_++;
                canAHealth_.noteRx(millis());
                frame = candidate;
                nextReadA_ = false;
                return true;
            }
            if (readFrom(canB_, DualCanRouting::canBBusLabel(), CAN_BUS_CAN_B, candidate))
            {
                canBRxCount_++;
                canBHealth_.noteRx(millis());
                frame = candidate;
                nextReadA_ = true;
                return true;
            }
        }
        else
        {
            if (readFrom(canB_, DualCanRouting::canBBusLabel(), CAN_BUS_CAN_B, candidate))
            {
                canBRxCount_++;
                canBHealth_.noteRx(millis());
                frame = candidate;
                nextReadA_ = true;
                return true;
            }
            if (readFrom(canA_, DualCanRouting::canABusLabel(), CAN_BUS_CAN_A, candidate))
            {
                canARxCount_++;
                canAHealth_.noteRx(millis());
                frame = candidate;
                nextReadA_ = false;
                return true;
            }
        }
        syncCanBFault();
        return false;
    }

    bool send(const CanFrame &frame) override
    {
        if (!sendAllowed(frame))
            return reportDenied(frame);

        // Child drivers re-check this same gate while holding their physical
        // controller locks. This closes the race with maintenance quiesce
        // after the aggregate pre-check but before the actual TX register call.
        canA_.allowSendFrame = allowSendFrame;
        canB_.allowSendFrame = allowSendFrame;

        const DualCanRouting::Targets targets = DualCanRouting::targetsForBus(frame.bus);
        if (!targets.canA && !targets.canB)
            return reportDenied(frame);

        // Do not short-circuit: an explicit combined mask must attempt both
        // physical buses and report aggregate success.
        bool okA = true;
        bool okB = true;
        const uint32_t now = millis();
        canAHealth_.observeController(canA_.ready());
        canBHealth_.observeController(canB_.ready());
        if (targets.canA)
        {
            bool attemptedA = false;
            okA = canAHealth_.txAllowed(now) && canA_.sendWithAttempt(frame, attemptedA);
            if (attemptedA)
                canAHealth_.noteTx(okA, now);
            if (okA)
                canATxCount_++;
            else if (attemptedA)
                canAErrorCount_++;
            reportAttempt(frame, CAN_BUS_CAN_A, okA, attemptedA);
        }
        if (targets.canB)
        {
            bool attemptedB = false;
            okB = canBHealth_.txAllowed(now) && canB_.sendWithAttempt(frame, attemptedB);
            syncCanBFault();
            if (attemptedB)
                canBHealth_.noteTx(okB, now);
            if (okB)
                canBTxCount_++;
            else if (attemptedB)
                canBErrorCount_++;
            reportAttempt(frame, CAN_BUS_CAN_B, okB, attemptedB);
        }
        return reportSend(frame, okA && okB);
    }

    bool reportsPhysicalTxAttempts() const override { return true; }

    bool physicalHealth(uint8_t bus, bool &ready, uint32_t &errors) const override
    {
        if (bus == CAN_BUS_CAN_A)
        {
            ready = canA_.ready();
            errors = canAErrorCount_ + canA_.healthErrorCount();
            return true;
        }
        if (bus == CAN_BUS_CAN_B)
        {
            ready = canB_.ready();
            errors = canBErrorCount_ + canB_.healthErrorCount();
            return true;
        }
        if (bus == CAN_BUS_ANY)
        {
            ready = canA_.ready() || canB_.ready();
            errors = canAErrorCount_ + canBErrorCount_ +
                     canA_.healthErrorCount() + canB_.healthErrorCount();
            return true;
        }
        return false;
    }

    void diagnosticsJson(char *out, size_t outLen) const override
    {
        if (!out || outLen == 0)
            return;
        const bool aReady = canA_.ready();
        const bool bReady = canB_.ready();
        snprintf(out, outLen,
                 "{\"type\":\"dual_t2can\",\"ready\":%s,"
                 "\"canA\":{\"bus\":0,\"ready\":%s,\"health\":\"%s\",\"quarantined\":%s,"
                 "\"errors\":%lu,\"tx\":%lu,\"rx\":%lu,\"lastRxMs\":%lu,\"lastTxMs\":%lu,"
                 "\"stalls\":%lu,\"quarantines\":%lu,\"recoveries\":%lu},"
                 "\"canB\":{\"bus\":1,\"ready\":%s,\"health\":\"%s\",\"quarantined\":%s,"
                 "\"errors\":%lu,\"tx\":%lu,\"rx\":%lu,\"lastRxMs\":%lu,\"lastTxMs\":%lu,"
                 "\"stalls\":%lu,\"quarantines\":%lu,\"recoveries\":%lu}}",
                 (aReady || bReady) ? "true" : "false", aReady ? "true" : "false",
                 CanBusHealth::stateName(canAHealth_.state()), canAHealth_.quarantined() ? "true" : "false",
                 static_cast<unsigned long>(canAErrorCount_),
                 static_cast<unsigned long>(canATxCount_),
                 static_cast<unsigned long>(canARxCount_),
                 static_cast<unsigned long>(canAHealth_.lastRxMs()),
                 static_cast<unsigned long>(canAHealth_.lastTxMs()),
                 static_cast<unsigned long>(canAHealth_.stallCount()),
                 static_cast<unsigned long>(canAHealth_.quarantineCount()),
                 static_cast<unsigned long>(canAHealth_.recoveryCount()),
                 bReady ? "true" : "false", CanBusHealth::stateName(canBHealth_.state()),
                 canBHealth_.quarantined() ? "true" : "false",
                 static_cast<unsigned long>(canBErrorCount_),
                 static_cast<unsigned long>(canBTxCount_),
                 static_cast<unsigned long>(canBRxCount_),
                 static_cast<unsigned long>(canBHealth_.lastRxMs()),
                 static_cast<unsigned long>(canBHealth_.lastTxMs()),
                 static_cast<unsigned long>(canBHealth_.stallCount()),
                 static_cast<unsigned long>(canBHealth_.quarantineCount()),
                 static_cast<unsigned long>(canBHealth_.recoveryCount()));
    }

    void diagnosticsSummary(char *out, size_t outLen) const override
    {
        if (!out || outLen == 0)
            return;
        char aConfig[96] = {};
        char bConfig[96] = {};
        canA_.configurationSummary(aConfig, sizeof(aConfig));
        canB_.configurationSummary(bConfig, sizeof(bConfig));
        const bool aReady = canA_.ready();
        const bool bReady = canB_.ready();
        snprintf(out, outLen,
                 "T-2CAN dual ready=%s canA(bus=0) ready=%s health=%s quarantined=%s "
                 "err=%lu tx=%lu rx=%lu %s; canB(bus=1) ready=%s health=%s quarantined=%s "
                 "err=%lu tx=%lu rx=%lu %s",
                 (aReady || bReady) ? "yes" : "no", aReady ? "yes" : "no",
                 CanBusHealth::stateName(canAHealth_.state()),
                 canAHealth_.quarantined() ? "yes" : "no",
                 static_cast<unsigned long>(canAErrorCount_),
                 static_cast<unsigned long>(canATxCount_),
                 static_cast<unsigned long>(canARxCount_), aConfig,
                 bReady ? "yes" : "no", CanBusHealth::stateName(canBHealth_.state()),
                 canBHealth_.quarantined() ? "yes" : "no",
                 static_cast<unsigned long>(canBErrorCount_),
                 static_cast<unsigned long>(canBTxCount_),
                 static_cast<unsigned long>(canBRxCount_), bConfig);
    }

    void configurationSummary(char *out, size_t outLen) const override
    {
        if (!out || outLen == 0)
            return;
        char aConfig[96] = {};
        char bConfig[96] = {};
        canA_.configurationSummary(aConfig, sizeof(aConfig));
        canB_.configurationSummary(bConfig, sizeof(bConfig));
        snprintf(out, outLen, "canA(bus=0) %s; canB(bus=1) %s", aConfig, bConfig);
    }

    ESP32_MCP2515Driver &canA() { return canA_; }
    TWAIDriver &canB() { return canB_; }

private:
    void syncCanBFault()
    {
        const uint32_t epoch = canB_.faultEpoch();
        if (epoch == canBFaultEpoch_)
            return;
        canBFaultEpoch_ = epoch;
        canBHealth_.noteControllerFault();
    }

    static bool readFrom(CanDriver &child, uint8_t busLabel, uint8_t physicalBus, CanFrame &frame)
    {
        if (!child.read(frame))
            return false;
        frame.bus = busLabel;
        frame.physicalBus = physicalBus;
        return true;
    }

    bool reportSend(const CanFrame &frame, bool ok)
    {
        if (onSendFrame)
            onSendFrame(frame, ok);
        return ok;
    }

    bool reportDenied(const CanFrame &frame)
    {
        if (onSendAttempt)
            onSendAttempt(frame, false, false);
        return reportSend(frame, false);
    }

    void reportAttempt(const CanFrame &frame, uint8_t bus, bool ok, bool attempted)
    {
        if (!onSendAttempt)
            return;
        CanFrame physical = frame;
        physical.physicalBus = bus;
        onSendAttempt(physical, ok, attempted);
    }

    ESP32_MCP2515Driver canA_;
    TWAIDriver canB_;
    bool nextReadA_ = true;
    uint32_t canAErrorCount_ = 0;
    uint32_t canBErrorCount_ = 0;
    uint32_t canATxCount_ = 0;
    uint32_t canBTxCount_ = 0;
    uint32_t canARxCount_ = 0;
    uint32_t canBRxCount_ = 0;
    CanBusHealth canAHealth_;
    CanBusHealth canBHealth_;
    uint32_t canBFaultEpoch_ = 0;
};
