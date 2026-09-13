#pragma once

#include <cstdint>

class OtaBootGuard
{
public:
    enum class State : uint8_t
    {
        Normal,
        Waiting,
        Confirmed,
        Rollback
    };

    enum class Action : uint8_t
    {
        None,
        Confirm,
        Rollback
    };

    static constexpr uint32_t kConfirmDeadlineMs = 30000;

    void begin(bool pendingVerification, uint32_t now)
    {
        startedMs_ = now;
        state_ = pendingVerification ? State::Waiting : State::Normal;
    }

    Action evaluate(bool preflightComplete, bool preflightPassed, uint32_t now) const
    {
        if (state_ != State::Waiting)
            return Action::None;
        if (preflightComplete)
            return preflightPassed ? Action::Confirm : Action::Rollback;
        if (now - startedMs_ >= kConfirmDeadlineMs)
            return Action::Rollback;
        return Action::None;
    }

    void markConfirmed() { state_ = State::Confirmed; }
    void markRollback() { state_ = State::Rollback; }

    bool pending() const { return state_ == State::Waiting; }
    bool txInhibited() const { return pending() || state_ == State::Rollback; }
    State state() const { return state_; }

    uint32_t remainingMs(uint32_t now) const
    {
        if (!pending())
            return 0;
        const uint32_t elapsed = now - startedMs_;
        return elapsed >= kConfirmDeadlineMs ? 0 : kConfirmDeadlineMs - elapsed;
    }

    static const char *stateName(State state)
    {
        switch (state)
        {
        case State::Waiting:
            return "pending";
        case State::Confirmed:
            return "confirmed";
        case State::Rollback:
            return "rollback";
        default:
            return "normal";
        }
    }

private:
    State state_ = State::Normal;
    uint32_t startedMs_ = 0;
};
