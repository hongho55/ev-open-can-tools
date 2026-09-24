#pragma once

#include "../can_frame_types.h"
#include <stddef.h>
#include <stdio.h>

struct CanDriver
{
    void (*onSendFrame)(const CanFrame &, bool ok) = nullptr;
    // Optional physical-attempt observation. For aggregate drivers, the frame
    // carries the physical bus label when attempted is true; attempted=false
    // represents a denied request with no physical transmission.
    void (*onSendAttempt)(const CanFrame &, bool ok, bool attempted) = nullptr;
    bool (*allowSendFrame)(const CanFrame &) = nullptr;

    virtual bool init() = 0;
    virtual void setFilters(const uint32_t *ids, uint8_t count) = 0;
    virtual bool enableInterrupt(void (*onReady)()) = 0;
    virtual bool read(CanFrame &frame) = 0;
    virtual bool send(const CanFrame &frame) = 0;
    // Hardware-capable drivers may provide the attempt result for this
    // invocation. The output is call-local so concurrent sends cannot
    // overwrite another send's attribution.
    virtual bool sendWithAttempt(const CanFrame &frame, bool &attempted)
    {
        attempted = false;
        return send(frame);
    }
    virtual bool ready() const { return true; }
    virtual void setMonitorAll(bool) {}
    // Stop controller activity and leave the physical TX output recessive before
    // restart/power transitions. Implementations may keep this as a no-op when
    // no controller has been initialized.
    virtual void shutdown() {}
    virtual void clearPendingTransmit() {}

    // Explicit, authenticated maintenance self-test. Implementations must not
    // place a frame on an attached physical CAN bus.
    virtual void selfTestJson(char *out, size_t outLen)
    {
        if (!out || outLen == 0)
            return;
        snprintf(out, outLen,
                 "{\"supported\":false,\"passed\":false,\"reason\":\"unsupported\"}");
    }

    bool sendAllowed(const CanFrame &frame) const
    {
        return !allowSendFrame || allowSendFrame(frame);
    }

    // Dev/test mode: route transmitted frames to a loopback (fire onSendFrame
    // only) instead of the bus, so a board with no transceiver stays quiet and
    // does not accumulate TX errors. No-op by default; drivers that talk to
    // hardware override this.
    virtual void setSimLoopback(bool /*enabled*/) {}
    virtual bool reportsPhysicalTxAttempts() const { return false; }
    virtual uint8_t physicalBus() const { return CAN_BUS_ANY; }
    virtual bool physicalHealth(uint8_t bus, bool &ready, uint32_t &errors) const
    {
        if (bus != CAN_BUS_ANY)
            return false;
        ready = this->ready();
        errors = healthErrorCount();
        return true;
    }
    virtual uint32_t healthErrorCount() const { return 0; }

    virtual void diagnosticsJson(char *out, size_t outLen) const
    {
        if (!out || outLen == 0)
            return;
        snprintf(out, outLen, "{\"type\":\"generic\"}");
    }

    virtual void diagnosticsSummary(char *out, size_t outLen) const
    {
        if (!out || outLen == 0)
            return;
        snprintf(out, outLen, "CAN driver diagnostics unavailable");
    }

    virtual void configurationSummary(char *out, size_t outLen) const
    {
        if (!out || outLen == 0)
            return;
        snprintf(out, outLen, "bitrate=500000 pins=unavailable");
    }

    virtual ~CanDriver() = default;
};
