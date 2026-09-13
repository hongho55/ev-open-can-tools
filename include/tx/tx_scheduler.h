#pragma once

#include <stdint.h>
#include <string.h>

#include "tx_intent.h"
#include "../drivers/can_driver.h"

namespace TxControl
{

struct Submission
{
    Result policy = {};
    bool driverCalled = false;
    bool driverSucceeded = false;
    bool physicalAttempt = false;
};

class Scheduler
{
public:
    using ContextProvider = PolicyContext (*)(void *opaque);

    Scheduler() = default;
    Scheduler(CanDriver *driver, ContextProvider provider, void *opaque = nullptr)
        : driver_(driver), provider_(provider), opaque_(opaque) {}

    void bind(CanDriver *driver, ContextProvider provider, void *opaque = nullptr)
    {
        driver_ = driver;
        provider_ = provider;
        opaque_ = opaque;
        cancelAll();
    }

    TxIntent prepare(Source source, uint16_t featureId, const CanFrame &frame,
                     uint8_t semanticBus, uint8_t physicalBus,
                     Session requiredSession, uint32_t lifetimeMs = 1000)
    {
        TxIntent intent;
        intent.source = source;
        intent.featureId = featureId;
        if (static_cast<uint8_t>(source) < kSourceCount)
            intent.requestId = ++lastIssuedRequestId_[static_cast<uint8_t>(source)];
        const PolicyContext context = currentContext();
        intent.sessionNonce = context.sessionNonce;
        intent.issuedAtMs = context.nowMs;
        intent.expiresAtMs = context.nowMs + lifetimeMs;
        intent.requiredSession = requiredSession;
        intent.semanticBus = semanticBus;
        intent.physicalBus = physicalBus;
        intent.expectedId = frame.id;
        intent.expectedDlc = frame.dlc;
        intent.frame = frame;
        intent.frame.bus = semanticBus;
        intent.frame.physicalBus = physicalBus;
        return intent;
    }

    Submission submit(const TxIntent &intent)
    {
        Submission out;
        if (!driver_ || !provider_)
        {
            out.policy = blocked(intent, Reason::InvalidRequest);
            return out;
        }

        const PolicyContext context = currentContext();
        out.policy = admission_.admit(intent, context);
        if (!out.policy.allowed)
            return out;

        bool attempted = false;
        out.driverCalled = true;
        out.driverSucceeded = driver_->sendWithAttempt(intent.frame, attempted);
        out.physicalAttempt = attempted;
        out.policy.physicalAttempt = attempted;
        return out;
    }

    void cancelAll()
    {
        admission_.reset();
        memset(lastIssuedRequestId_, 0, sizeof(lastIssuedRequestId_));
    }

private:
    static constexpr uint8_t kSourceCount = 5;

    PolicyContext currentContext() const
    {
        return provider_ ? provider_(opaque_) : PolicyContext{};
    }

    CanDriver *driver_ = nullptr;
    ContextProvider provider_ = nullptr;
    void *opaque_ = nullptr;
    Admission admission_;
    uint64_t lastIssuedRequestId_[kSourceCount] = {};
};

} // namespace TxControl
