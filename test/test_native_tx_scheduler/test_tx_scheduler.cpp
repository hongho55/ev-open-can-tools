#include <assert.h>

#include "tx/tx_scheduler.h"

using namespace TxControl;

class AttemptDriver : public CanDriver
{
public:
    int calls = 0;
    bool init() override { return true; }
    void setFilters(const uint32_t *, uint8_t) override {}
    bool enableInterrupt(void (*)()) override { return false; }
    bool read(CanFrame &) override { return false; }
    bool send(const CanFrame &) override { ++calls; return true; }
    bool sendWithAttempt(const CanFrame &frame, bool &attempted) override
    {
        attempted = true;
        return send(frame);
    }
};

struct Runtime
{
    uint32_t now = 0;
    uint32_t nonce = 0;
};

static PolicyContext policy(void *opaque)
{
    const Runtime &runtime = *static_cast<Runtime *>(opaque);
    PolicyContext out;
    out.nowMs = runtime.now;
    out.sessionNonce = runtime.nonce;
    out.session = Session::Active;
    out.masterEnabled = true;
    out.startupFresh = true;
    out.vehicleFresh = true;
    out.busHealthy = true;
    return out;
}

static TxIntent nagIntent(Scheduler &scheduler)
{
    CanFrame frame{};
    frame.id = 0x370;
    frame.dlc = 8;
    TxIntent intent = scheduler.prepare(Source::BuiltIn, 0x370, frame,
                                        CAN_BUS_PARTY, CAN_BUS_CAN_A,
                                        Session::Active, 100);
    intent.cadenceMs = 5;
    intent.maxBurst = 200;
    intent.cooldownMs = 1000;
    intent.counter = CounterStrategy::IncrementObserved;
    intent.checksum = ChecksumStrategy::VerifiedGenerator;
    return intent;
}

int main()
{
    AttemptDriver driver;
    Runtime runtime{100, 77};
    Scheduler scheduler(&driver, policy, &runtime);

    TxIntent nag = nagIntent(scheduler);
    Submission sent = scheduler.submit(nag);
    assert(sent.policy.allowed);
    assert(sent.driverCalled);
    assert(sent.driverSucceeded);
    assert(sent.physicalAttempt);
    assert(driver.calls == 1);

    TxIntent tooSoon = nagIntent(scheduler);
    runtime.now = 102;
    Submission cadence = scheduler.submit(tooSoon);
    assert(cadence.policy.reason == Reason::Cadence);
    assert(!cadence.driverCalled);
    assert(driver.calls == 1);

    runtime.now = 110;
    TxIntent research = nagIntent(scheduler);
    research.expectedId = research.frame.id = 0x247;
    Submission denied = scheduler.submit(research);
    assert(denied.policy.reason == Reason::SemanticDeny);
    assert(!denied.driverCalled);
    assert(driver.calls == 1);

    runtime.now = 115;
    TxIntent staleSession = nagIntent(scheduler);
    runtime.nonce = 78;
    scheduler.cancelAll();
    Submission stale = scheduler.submit(staleSession);
    assert(stale.policy.reason == Reason::InvalidRequest);
    assert(!stale.driverCalled);
    assert(driver.calls == 1);

    TxIntent wrongPhysical = nagIntent(scheduler);
    wrongPhysical.frame.physicalBus = CAN_BUS_CAN_B;
    Submission mismatched = scheduler.submit(wrongPhysical);
    assert(mismatched.policy.reason == Reason::FrameMismatch);
    assert(!mismatched.driverCalled);
    assert(driver.calls == 1);

    scheduler.cancelAll();
    runtime.now = 120;
    Submission afterCancel = scheduler.submit(nagIntent(scheduler));
    assert(afterCancel.driverSucceeded);
    assert(driver.calls == 2);
    return 0;
}
