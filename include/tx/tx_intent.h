#pragma once

#include <stdint.h>
#include <string.h>

#include "../can_frame_types.h"

namespace TxControl
{

static constexpr uint32_t kMaxIntentLifetimeMs = 30U * 1000U;
static constexpr uint8_t kAdmissionSlots = 8;

enum class Source : uint8_t { BuiltIn, Plugin, Web, Ble, CanTest };
enum class Session : uint8_t { Observe, Active, Bench, Maintenance };
enum class CounterStrategy : uint8_t { Unknown, PreserveObserved, IncrementObserved, VerifiedGenerator };
enum class ChecksumStrategy : uint8_t { Unknown, PreserveObserved, VerifiedGenerator };

enum Requirement : uint16_t
{
    RequireStartupFresh = 1U << 0,
    RequireVehicleFresh = 1U << 1,
    RequireParked = 1U << 2,
    RequireStationary = 1U << 3,
    RequireControlConnected = 1U << 4,
};

struct MuxConstraint
{
    bool present = false;
    uint8_t byte = 0;
    uint8_t mask = 0;
    uint8_t value = 0;
};

struct TxIntent
{
    Source source = Source::BuiltIn;
    uint16_t featureId = 0;
    uint64_t requestId = 0;
    uint32_t sessionNonce = 0;
    uint32_t issuedAtMs = 0;
    uint32_t expiresAtMs = 0;
    Session requiredSession = Session::Active;
    uint8_t semanticBus = CAN_BUS_ANY;
    uint8_t physicalBus = CAN_BUS_ANY;
    uint32_t expectedId = 0;
    uint8_t expectedDlc = 0;
    MuxConstraint mux = {};
    uint16_t cadenceMs = 0;
    uint8_t maxBurst = 1;
    uint16_t cooldownMs = 0;
    CounterStrategy counter = CounterStrategy::Unknown;
    ChecksumStrategy checksum = ChecksumStrategy::Unknown;
    uint16_t requirements = RequireStartupFresh | RequireVehicleFresh;
    CanFrame frame = {};
};

struct PolicyContext
{
    uint32_t nowMs = 0;
    uint32_t sessionNonce = 0;
    Session session = Session::Observe;
    bool masterEnabled = false;
    bool startupFresh = false;
    bool vehicleFresh = false;
    bool parked = false;
    bool stationary = false;
    bool busHealthy = false;
    bool otaInhibit = false;
    bool controlAuthorized = false;
    bool controlConnected = false;
    bool benchIsolated = false;
};

enum class Reason : uint8_t
{
    Allowed,
    InvalidRequest,
    Expired,
    Replay,
    WrongSession,
    MasterDisabled,
    OtaInhibit,
    Unauthorized,
    ControlDisconnected,
    StartupStale,
    VehicleStale,
    StateBlocked,
    BusUnhealthy,
    InvalidRoute,
    FrameMismatch,
    UnknownIntegrityStrategy,
    SemanticDeny,
    BenchNotIsolated,
    Cadence,
    BurstLimit,
    Cooldown,
    Capacity,
};

struct Result
{
    bool allowed = false;
    bool physicalAttempt = false;
    Reason reason = Reason::InvalidRequest;
    uint16_t featureId = 0;
    uint64_t requestId = 0;
};

inline const char *reasonName(Reason reason)
{
    switch (reason)
    {
    case Reason::Allowed: return "allowed";
    case Reason::InvalidRequest: return "invalid_request";
    case Reason::Expired: return "expired";
    case Reason::Replay: return "replay";
    case Reason::WrongSession: return "wrong_session";
    case Reason::MasterDisabled: return "master_disabled";
    case Reason::OtaInhibit: return "ota_inhibit";
    case Reason::Unauthorized: return "unauthorized";
    case Reason::ControlDisconnected: return "control_disconnected";
    case Reason::StartupStale: return "startup_stale";
    case Reason::VehicleStale: return "vehicle_stale";
    case Reason::StateBlocked: return "state_blocked";
    case Reason::BusUnhealthy: return "bus_unhealthy";
    case Reason::InvalidRoute: return "invalid_route";
    case Reason::FrameMismatch: return "frame_mismatch";
    case Reason::UnknownIntegrityStrategy: return "unknown_integrity_strategy";
    case Reason::SemanticDeny: return "semantic_deny";
    case Reason::BenchNotIsolated: return "bench_not_isolated";
    case Reason::Cadence: return "cadence";
    case Reason::BurstLimit: return "burst_limit";
    case Reason::Cooldown: return "cooldown";
    case Reason::Capacity: return "capacity";
    }
    return "invalid_request";
}

inline bool externalSource(Source source)
{
    return source == Source::Web || source == Source::Ble || source == Source::CanTest;
}

inline Result blocked(const TxIntent &intent, Reason reason)
{
    Result out;
    out.reason = reason;
    out.featureId = intent.featureId;
    out.requestId = intent.requestId;
    return out;
}

inline Result evaluate(const TxIntent &intent, const PolicyContext &context)
{
    if (static_cast<uint8_t>(intent.source) > static_cast<uint8_t>(Source::CanTest) ||
        intent.featureId == 0 || intent.requestId == 0 || intent.sessionNonce == 0 ||
        intent.sessionNonce != context.sessionNonce || intent.maxBurst == 0)
        return blocked(intent, Reason::InvalidRequest);
    const uint32_t lifetime = intent.expiresAtMs - intent.issuedAtMs;
    if (lifetime == 0 || lifetime > kMaxIntentLifetimeMs ||
        static_cast<int32_t>(context.nowMs - intent.issuedAtMs) < 0)
        return blocked(intent, Reason::InvalidRequest);
    if (static_cast<int32_t>(intent.expiresAtMs - context.nowMs) <= 0)
        return blocked(intent, Reason::Expired);
    if (context.otaInhibit || context.session == Session::Maintenance)
        return blocked(intent, Reason::OtaInhibit);
    if (context.session != intent.requiredSession)
        return blocked(intent, Reason::WrongSession);
    if (context.session == Session::Bench && !context.benchIsolated)
        return blocked(intent, Reason::BenchNotIsolated);
    if (!context.masterEnabled) return blocked(intent, Reason::MasterDisabled);
    if (externalSource(intent.source) && !context.controlAuthorized)
        return blocked(intent, Reason::Unauthorized);
    if ((intent.requirements & RequireControlConnected) && !context.controlConnected)
        return blocked(intent, Reason::ControlDisconnected);
    if ((intent.requirements & RequireStartupFresh) && !context.startupFresh)
        return blocked(intent, Reason::StartupStale);
    if ((intent.requirements & RequireVehicleFresh) && !context.vehicleFresh)
        return blocked(intent, Reason::VehicleStale);
    if ((intent.requirements & RequireParked) && !context.parked)
        return blocked(intent, Reason::StateBlocked);
    if ((intent.requirements & RequireStationary) && !context.stationary)
        return blocked(intent, Reason::StateBlocked);
    if (!context.busHealthy) return blocked(intent, Reason::BusUnhealthy);
    if ((intent.semanticBus != CAN_BUS_CH && intent.semanticBus != CAN_BUS_VEH &&
         intent.semanticBus != CAN_BUS_PARTY) ||
        (intent.physicalBus != CAN_BUS_CAN_A && intent.physicalBus != CAN_BUS_CAN_B))
        return blocked(intent, Reason::InvalidRoute);
    if (intent.expectedId > 0x7ff || intent.expectedDlc > 8 ||
        intent.frame.id != intent.expectedId || intent.frame.dlc != intent.expectedDlc ||
        intent.frame.bus != intent.semanticBus)
        return blocked(intent, Reason::FrameMismatch);
    if (intent.mux.present &&
        (intent.mux.byte >= intent.frame.dlc ||
         (intent.frame.data[intent.mux.byte] & intent.mux.mask) != intent.mux.value))
        return blocked(intent, Reason::FrameMismatch);
    if (intent.counter == CounterStrategy::Unknown ||
        intent.checksum == ChecksumStrategy::Unknown)
        return blocked(intent, Reason::UnknownIntegrityStrategy);
    // right-stalk/Park remains read-only until independently qualified.
    if (intent.expectedId == 0x229) return blocked(intent, Reason::SemanticDeny);

    Result out;
    out.allowed = true;
    out.reason = Reason::Allowed;
    out.featureId = intent.featureId;
    out.requestId = intent.requestId;
    return out;
}

class Admission
{
public:
    Result admit(const TxIntent &intent, const PolicyContext &context)
    {
        Result result = evaluate(intent, context);
        if (!result.allowed) return result;
        if (seenRequest(intent.source, intent.requestId)) return blocked(intent, Reason::Replay);

        Slot *slot = slotFor(intent.featureId);
        if (!slot) return blocked(intent, Reason::Capacity);
        if (slot->used)
        {
            const uint32_t sinceLast = context.nowMs - slot->lastAcceptedMs;
            if (intent.cadenceMs && sinceLast < intent.cadenceMs)
                return blocked(intent, Reason::Cadence);
            if (slot->burstCount >= intent.maxBurst)
            {
                if (!intent.cooldownMs)
                    return blocked(intent, Reason::BurstLimit);
                if (sinceLast < intent.cooldownMs)
                    return blocked(intent, Reason::Cooldown);
                slot->burstCount = 0;
            }
        }

        rememberRequest(intent.source, intent.requestId);
        slot->used = true;
        slot->featureId = intent.featureId;
        slot->lastAcceptedMs = context.nowMs;
        ++slot->burstCount;
        return result;
    }

    void reset()
    {
        memset(slots_, 0, sizeof(slots_));
        memset(lastRequestId_, 0, sizeof(lastRequestId_));
    }

private:
    struct Slot
    {
        uint16_t featureId = 0;
        uint32_t lastAcceptedMs = 0;
        uint8_t burstCount = 0;
        bool used = false;
    };

    bool seenRequest(Source source, uint64_t requestId) const
    {
        return requestId <= lastRequestId_[static_cast<uint8_t>(source)];
    }

    void rememberRequest(Source source, uint64_t requestId)
    {
        lastRequestId_[static_cast<uint8_t>(source)] = requestId;
    }

    Slot *slotFor(uint16_t featureId)
    {
        for (uint8_t i = 0; i < kAdmissionSlots; ++i)
            if (slots_[i].used && slots_[i].featureId == featureId) return &slots_[i];
        for (uint8_t i = 0; i < kAdmissionSlots; ++i)
            if (!slots_[i].used) return &slots_[i];
        return nullptr;
    }

    Slot slots_[kAdmissionSlots] = {};
    uint64_t lastRequestId_[5] = {};
};

} // namespace TxControl
