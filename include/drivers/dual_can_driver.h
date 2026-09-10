#pragma once

#include <cstdio>
#include <cstring>

#include "../can_frame_types.h"
#include "can_driver.h"
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
        canAReady_ = canA_.init();
        canBReady_ = canB_.init();
        return canAReady_ || canBReady_;
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

    void setSimLoopback(bool enabled) override
    {
        canA_.setSimLoopback(enabled);
        canB_.setSimLoopback(enabled);
    }

    bool read(CanFrame &frame) override
    {
        CanFrame candidate;
        if (nextReadA_)
        {
            if (readFrom(canA_, DualCanRouting::canABusLabel(), candidate))
            {
                canARxCount_++;
                frame = candidate;
                nextReadA_ = false;
                return true;
            }
            if (readFrom(canB_, DualCanRouting::canBBusLabel(), candidate))
            {
                canBRxCount_++;
                frame = candidate;
                nextReadA_ = true;
                return true;
            }
        }
        else
        {
            if (readFrom(canB_, DualCanRouting::canBBusLabel(), candidate))
            {
                canBRxCount_++;
                frame = candidate;
                nextReadA_ = true;
                return true;
            }
            if (readFrom(canA_, DualCanRouting::canABusLabel(), candidate))
            {
                canARxCount_++;
                frame = candidate;
                nextReadA_ = false;
                return true;
            }
        }
        return false;
    }

    bool send(const CanFrame &frame) override
    {
        if (!sendAllowed(frame))
            return reportSend(frame, false);

        const DualCanRouting::Targets targets = DualCanRouting::targetsForBus(frame.bus);
        if (!targets.canA && !targets.canB)
            return reportSend(frame, false);

        // Do not short-circuit: an explicit combined mask must attempt both
        // physical buses and report aggregate success.
        bool okA = true;
        bool okB = true;
        if (targets.canA)
        {
            okA = canA_.send(frame);
            okA ? canATxCount_++ : canAErrorCount_++;
        }
        if (targets.canB)
        {
            okB = canB_.send(frame);
            okB ? canBTxCount_++ : canBErrorCount_++;
        }
        return reportSend(frame, okA && okB);
    }

    void diagnosticsJson(char *out, size_t outLen) const override
    {
        if (!out || outLen == 0)
            return;
        const bool aReady = canA_.ready();
        const bool bReady = canB_.ready();
        snprintf(out, outLen,
                 "{\"type\":\"dual_t2can\",\"ready\":%s,"
                 "\"canA\":{\"bus\":0,\"ready\":%s,\"health\":\"%s\","
                 "\"errors\":%lu,\"tx\":%lu,\"rx\":%lu},"
                 "\"canB\":{\"bus\":1,\"ready\":%s,\"health\":\"%s\","
                 "\"errors\":%lu,\"tx\":%lu,\"rx\":%lu}}",
                 (aReady || bReady) ? "true" : "false", aReady ? "true" : "false",
                 aReady ? "ready" : "offline",
                 static_cast<unsigned long>(canAErrorCount_),
                 static_cast<unsigned long>(canATxCount_),
                 static_cast<unsigned long>(canARxCount_),
                 bReady ? "true" : "false", bReady ? "ready" : "offline",
                 static_cast<unsigned long>(canBErrorCount_),
                 static_cast<unsigned long>(canBTxCount_),
                 static_cast<unsigned long>(canBRxCount_));
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
                 "T-2CAN dual ready=%s canA(bus=0) ready=%s health=%s "
                 "err=%lu tx=%lu rx=%lu %s; canB(bus=1) ready=%s health=%s "
                 "err=%lu tx=%lu rx=%lu %s",
                 (aReady || bReady) ? "yes" : "no", aReady ? "yes" : "no",
                 aReady ? "ready" : "offline",
                 static_cast<unsigned long>(canAErrorCount_),
                 static_cast<unsigned long>(canATxCount_),
                 static_cast<unsigned long>(canARxCount_), aConfig,
                 bReady ? "yes" : "no", bReady ? "ready" : "offline",
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
    static bool readFrom(CanDriver &child, uint8_t busLabel, CanFrame &frame)
    {
        if (!child.read(frame))
            return false;
        frame.bus = busLabel;
        return true;
    }

    bool reportSend(const CanFrame &frame, bool ok)
    {
        if (onSendFrame)
            onSendFrame(frame, ok);
        return ok;
    }

    ESP32_MCP2515Driver canA_;
    TWAIDriver canB_;
    bool canAReady_ = false;
    bool canBReady_ = false;
    bool nextReadA_ = true;
    uint32_t canAErrorCount_ = 0;
    uint32_t canBErrorCount_ = 0;
    uint32_t canATxCount_ = 0;
    uint32_t canBTxCount_ = 0;
    uint32_t canARxCount_ = 0;
    uint32_t canBRxCount_ = 0;
};
