#pragma once

#include <stdint.h>

class CanBusHealth
{
public:
    enum class State : uint8_t
    {
        Starting,
        Healthy,
        RxStalled,
        BusError,
        Recovered,
    };

    static constexpr uint32_t kRxStallMs = 2000;
    static constexpr uint8_t kTxFailureLimit = 3;

    void reset(bool controllerReady)
    {
        controllerReady_ = controllerReady;
        seenRx_ = false;
        quarantined_ = true;
        state_ = controllerReady ? State::Starting : State::BusError;
        lastRxMs_ = 0;
        lastTxMs_ = 0;
        consecutiveTxFailures_ = 0;
    }

    void observeController(bool ready)
    {
        if (ready == controllerReady_)
            return;
        controllerReady_ = ready;
        quarantined_ = true;
        if (ready)
        {
            state_ = State::Recovered;
            ++recoveryCount_;
        }
        else
        {
            state_ = State::BusError;
            ++quarantineCount_;
        }
    }

    void noteRx(uint32_t now)
    {
        if (quarantined_ && state_ == State::BusError && seenRx_)
            ++recoveryCount_;
        lastRxMs_ = now;
        ++rxCount_;
        seenRx_ = true;
        consecutiveTxFailures_ = 0;
        if (controllerReady_)
        {
            quarantined_ = false;
            state_ = State::Healthy;
        }
    }

    void noteTx(bool ok, uint32_t now)
    {
        lastTxMs_ = now;
        if (ok)
        {
            ++txSuccessCount_;
            consecutiveTxFailures_ = 0;
            return;
        }
        ++txFailureCount_;
        if (++consecutiveTxFailures_ >= kTxFailureLimit && !quarantined_)
        {
            quarantined_ = true;
            state_ = State::BusError;
            ++quarantineCount_;
        }
    }

    void noteControllerFault()
    {
        if (!quarantined_)
            ++quarantineCount_;
        quarantined_ = true;
        state_ = State::BusError;
    }

    bool txAllowed(uint32_t now)
    {
        if (!controllerReady_ || !seenRx_ || quarantined_)
            return false;
        if (now - lastRxMs_ > kRxStallMs)
        {
            quarantined_ = true;
            state_ = State::RxStalled;
            ++stallCount_;
            ++quarantineCount_;
            return false;
        }
        return true;
    }

    static const char *stateName(State state)
    {
        switch (state)
        {
        case State::Starting: return "starting";
        case State::Healthy: return "healthy";
        case State::RxStalled: return "rx_stalled";
        case State::BusError: return "bus_error";
        case State::Recovered: return "recovered";
        }
        return "unknown";
    }

    State state() const { return state_; }
    bool quarantined() const { return quarantined_; }
    uint32_t lastRxMs() const { return lastRxMs_; }
    uint32_t lastTxMs() const { return lastTxMs_; }
    uint32_t rxCount() const { return rxCount_; }
    uint32_t txSuccessCount() const { return txSuccessCount_; }
    uint32_t txFailureCount() const { return txFailureCount_; }
    uint32_t stallCount() const { return stallCount_; }
    uint32_t quarantineCount() const { return quarantineCount_; }
    uint32_t recoveryCount() const { return recoveryCount_; }

private:
    State state_ = State::Starting;
    bool controllerReady_ = false;
    bool seenRx_ = false;
    bool quarantined_ = true;
    uint8_t consecutiveTxFailures_ = 0;
    uint32_t lastRxMs_ = 0;
    uint32_t lastTxMs_ = 0;
    uint32_t rxCount_ = 0;
    uint32_t txSuccessCount_ = 0;
    uint32_t txFailureCount_ = 0;
    uint32_t stallCount_ = 0;
    uint32_t quarantineCount_ = 0;
    uint32_t recoveryCount_ = 0;
};
