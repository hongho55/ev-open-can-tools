#pragma once

#include <stdint.h>
#include <string.h>

namespace BleOwner
{

static constexpr uint32_t kEnrollmentWindowMs = 5U * 60U * 1000U;
static constexpr uint32_t kMaxRequestLifetimeMs = 30U * 1000U;
static constexpr uint8_t kReplaySlots = 8;

enum Permission : uint8_t
{
    Diagnostics = 1U << 0,
    Ota = 1U << 1,
    CanArm = 1U << 2,
    Admin = 1U << 3,
};

static constexpr uint8_t kAllPermissions =
    static_cast<uint8_t>(Diagnostics | Ota | CanArm | Admin);

struct Peer
{
    uint8_t addressType = 0;
    uint8_t address[6] = {};
    bool encrypted = false;
    bool authenticated = false;
    bool bonded = false;
};

struct Request
{
    uint64_t id = 0;
    uint32_t generation = 0;
    uint32_t issuedAtMs = 0;
    uint32_t expiresAtMs = 0;
};

enum class Decision : uint8_t
{
    Allowed,
    NoOwner,
    EnrollmentClosed,
    InsecureLink,
    WrongOwner,
    PermissionDenied,
    InvalidRequest,
    Replay,
};

struct Status
{
    bool enrolled = false;
    bool enrollmentOpen = false;
    uint8_t permissions = 0;
    uint32_t enrollmentRemainingMs = 0;
    uint64_t fingerprint = 0;
    uint32_t generation = 0;
};

inline bool secureBond(const Peer &peer)
{
    return peer.encrypted && peer.authenticated && peer.bonded;
}

inline bool sameIdentity(const Peer &a, const Peer &b)
{
    uint8_t different = static_cast<uint8_t>(a.addressType ^ b.addressType);
    for (uint8_t i = 0; i < 6; ++i)
        different |= static_cast<uint8_t>(a.address[i] ^ b.address[i]);
    return different == 0;
}

// Diagnostic fingerprint only. Authentication uses the bonded BLE identity,
// never this non-secret digest.
inline uint64_t fingerprint(const Peer &peer)
{
    uint64_t hash = 1469598103934665603ULL;
    hash ^= peer.addressType;
    hash *= 1099511628211ULL;
    for (uint8_t i = 0; i < 6; ++i)
    {
        hash ^= peer.address[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

inline const char *decisionName(Decision decision)
{
    switch (decision)
    {
    case Decision::Allowed: return "allowed";
    case Decision::NoOwner: return "no_owner";
    case Decision::EnrollmentClosed: return "enrollment_closed";
    case Decision::InsecureLink: return "insecure_link";
    case Decision::WrongOwner: return "wrong_owner";
    case Decision::PermissionDenied: return "permission_denied";
    case Decision::InvalidRequest: return "invalid_request";
    case Decision::Replay: return "replay";
    }
    return "invalid_request";
}

class Authorization
{
public:
    void begin(uint32_t nowMs)
    {
        bootMs_ = nowMs;
        replayCount_ = 0;
        replayNext_ = 0;
    }

    // Persistence adapters may restore only identity, permissions and generation.
    // Link security flags are runtime facts and are deliberately discarded.
    void restore(const Peer &owner, uint8_t permissions, uint32_t generation)
    {
        owner_ = owner;
        owner_.encrypted = false;
        owner_.authenticated = false;
        owner_.bonded = false;
        permissions_ = static_cast<uint8_t>((permissions & kAllPermissions) | Admin);
        generation_ = generation;
        enrolled_ = true;
        enrollmentConsumed_ = true;
        replayCount_ = 0;
        replayNext_ = 0;
    }

    void restoreRevoked(uint32_t generation)
    {
        memset(&owner_, 0, sizeof(owner_));
        permissions_ = 0;
        generation_ = generation;
        enrolled_ = false;
        enrollmentConsumed_ = true;
        replayCount_ = 0;
        replayNext_ = 0;
    }

    Decision enroll(const Peer &peer, uint32_t nowMs)
    {
        if (enrolled_) return Decision::WrongOwner;
        if (!secureBond(peer)) return Decision::InsecureLink;
        if (!enrollmentOpen(nowMs)) return Decision::EnrollmentClosed;
        owner_ = peer;
        owner_.encrypted = false;
        owner_.authenticated = false;
        owner_.bonded = false;
        permissions_ = static_cast<uint8_t>(Diagnostics | Admin);
        ++generation_;
        enrolled_ = true;
        enrollmentConsumed_ = true;
        replayCount_ = 0;
        replayNext_ = 0;
        return Decision::Allowed;
    }

    Decision authorize(const Peer &peer, Permission permission,
                       const Request &request, uint32_t nowMs)
    {
        if (!enrolled_) return Decision::NoOwner;
        if (!secureBond(peer)) return Decision::InsecureLink;
        if (!sameIdentity(owner_, peer)) return Decision::WrongOwner;
        if ((permissions_ & static_cast<uint8_t>(permission)) == 0)
            return Decision::PermissionDenied;
        if (!validRequest(request, nowMs)) return Decision::InvalidRequest;
        if (seen(request.id)) return Decision::Replay;
        remember(request.id);
        return Decision::Allowed;
    }

    Decision updatePermissions(const Peer &peer, const Request &request,
                               uint8_t permissions, uint32_t nowMs)
    {
        const Decision decision = authorize(peer, Admin, request, nowMs);
        if (decision != Decision::Allowed) return decision;
        // Admin remains present so an owner cannot create an unrecoverable record.
        permissions_ = static_cast<uint8_t>((permissions & kAllPermissions) | Admin);
        ++generation_;
        return Decision::Allowed;
    }

    Decision revoke(const Peer &peer, const Request &request, uint32_t nowMs)
    {
        const Decision decision = authorize(peer, Admin, request, nowMs);
        if (decision != Decision::Allowed) return decision;
        enrolled_ = false;
        permissions_ = 0;
        memset(&owner_, 0, sizeof(owner_));
        ++generation_;
        replayCount_ = 0;
        replayNext_ = 0;
        // Revocation does not silently reopen first-boot enrollment.
        return Decision::Allowed;
    }

    Decision beginReplacement(const Peer &peer, const Request &request,
                              uint32_t nowMs)
    {
        const Decision decision = authorize(peer, Admin, request, nowMs);
        if (decision != Decision::Allowed) return decision;
        enrolled_ = false;
        permissions_ = 0;
        memset(&owner_, 0, sizeof(owner_));
        ++generation_;
        bootMs_ = nowMs;
        enrollmentConsumed_ = false;
        replayCount_ = 0;
        replayNext_ = 0;
        return Decision::Allowed;
    }

    Status status(uint32_t nowMs) const
    {
        Status out;
        out.enrolled = enrolled_;
        out.enrollmentOpen = enrollmentOpen(nowMs);
        out.permissions = permissions_;
        out.generation = generation_;
        out.fingerprint = enrolled_ ? BleOwner::fingerprint(owner_) : 0;
        if (out.enrollmentOpen)
            out.enrollmentRemainingMs = kEnrollmentWindowMs - (nowMs - bootMs_);
        return out;
    }

    const Peer &ownerRecord() const { return owner_; }
    uint8_t permissions() const { return permissions_; }
    uint32_t generation() const { return generation_; }

private:
    bool enrollmentOpen(uint32_t nowMs) const
    {
        return !enrolled_ && !enrollmentConsumed_ &&
               static_cast<uint32_t>(nowMs - bootMs_) < kEnrollmentWindowMs;
    }

    bool validRequest(const Request &request, uint32_t nowMs) const
    {
        if (request.id == 0) return false;
        if (request.generation != generation_) return false;
        const uint32_t lifetime = request.expiresAtMs - request.issuedAtMs;
        if (lifetime == 0 || lifetime > kMaxRequestLifetimeMs) return false;
        if (static_cast<int32_t>(nowMs - request.issuedAtMs) < 0) return false;
        if (static_cast<int32_t>(request.expiresAtMs - nowMs) <= 0) return false;
        return true;
    }

    bool seen(uint64_t requestId) const
    {
        for (uint8_t i = 0; i < replayCount_; ++i)
            if (replayIds_[i] == requestId) return true;
        return false;
    }

    void remember(uint64_t requestId)
    {
        replayIds_[replayNext_] = requestId;
        replayNext_ = static_cast<uint8_t>((replayNext_ + 1) % kReplaySlots);
        if (replayCount_ < kReplaySlots) ++replayCount_;
    }

    Peer owner_ = {};
    uint64_t replayIds_[kReplaySlots] = {};
    uint32_t bootMs_ = 0;
    uint32_t generation_ = 0;
    uint8_t permissions_ = 0;
    uint8_t replayCount_ = 0;
    uint8_t replayNext_ = 0;
    bool enrolled_ = false;
    bool enrollmentConsumed_ = false;
};

} // namespace BleOwner
