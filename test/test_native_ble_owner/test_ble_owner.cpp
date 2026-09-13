#include <assert.h>
#include <string.h>

#include "ble/owner_authorization.h"

using namespace BleOwner;

static Peer peer(uint8_t seed, bool secure = true)
{
    Peer out;
    out.addressType = 1;
    for (uint8_t i = 0; i < 6; ++i) out.address[i] = static_cast<uint8_t>(seed + i);
    out.encrypted = secure;
    out.authenticated = secure;
    out.bonded = secure;
    return out;
}

static Request request(uint64_t id, uint32_t issuedAt, uint32_t expiresAt,
                       uint32_t generation = 1)
{
    Request out;
    out.id = id;
    out.generation = generation;
    out.issuedAtMs = issuedAt;
    out.expiresAtMs = expiresAt;
    return out;
}

int main()
{
    const Peer owner = peer(10);
    const Peer stranger = peer(30);

    Authorization auth;
    auth.begin(1000);
    Status initial = auth.status(1000);
    assert(!initial.enrolled);
    assert(initial.enrollmentOpen);
    assert(initial.enrollmentRemainingMs == kEnrollmentWindowMs);

    assert(auth.enroll(peer(10, false), 1100) == Decision::InsecureLink);
    assert(auth.enroll(owner, 1100) == Decision::Allowed);
    Status enrolled = auth.status(1100);
    assert(enrolled.enrolled);
    assert(!enrolled.enrollmentOpen);
    assert(enrolled.permissions == static_cast<uint8_t>(Diagnostics | Admin));
    assert(enrolled.fingerprint != 0);

    const Request diagnostic = request(1, 1200, 2200);
    assert(auth.authorize(stranger, Diagnostics, diagnostic, 1300) == Decision::WrongOwner);
    assert(auth.authorize(owner, Ota, diagnostic, 1300) == Decision::PermissionDenied);
    assert(auth.authorize(owner, Diagnostics, diagnostic, 1300) == Decision::Allowed);
    assert(auth.authorize(owner, Diagnostics, diagnostic, 1301) == Decision::Replay);

    assert(auth.authorize(owner, Diagnostics, request(0, 1200, 2200), 1300) ==
           Decision::InvalidRequest);
    assert(auth.authorize(owner, Diagnostics, request(2, 1400, 2200), 1300) ==
           Decision::InvalidRequest);
    assert(auth.authorize(owner, Diagnostics, request(3, 1000, 1300), 1300) ==
           Decision::InvalidRequest);
    assert(auth.authorize(owner, Diagnostics,
                          request(4, 1000, 1000 + kMaxRequestLifetimeMs + 1), 1300) ==
           Decision::InvalidRequest);

    const Request permissions = request(5, 1300, 2300);
    assert(auth.updatePermissions(owner, permissions,
                                  static_cast<uint8_t>(Diagnostics | Ota | CanArm | 0x80),
                                  1400) == Decision::Allowed);
    assert(auth.permissions() == kAllPermissions);
    assert(auth.authorize(owner, CanArm, request(6, 1400, 2400, 2), 1500) ==
           Decision::Allowed);
    assert(auth.authorize(owner, Ota, request(7, 1400, 2400, 2), 1500) ==
           Decision::Allowed);
    assert(auth.authorize(owner, Ota, request(70, 1400, 2400, 1), 1500) ==
           Decision::InvalidRequest);

    const uint32_t generationBeforeRevoke = auth.generation();
    assert(auth.revoke(owner, request(8, 1500, 2500, 2), 1600) == Decision::Allowed);
    Status revoked = auth.status(1600);
    assert(!revoked.enrolled);
    assert(!revoked.enrollmentOpen); // Revocation never reopens first enrollment.
    assert(revoked.permissions == 0);
    assert(revoked.fingerprint == 0);
    assert(revoked.generation == generationBeforeRevoke + 1);
    assert(auth.authorize(owner, Diagnostics, request(9, 1600, 2600), 1700) ==
           Decision::NoOwner);

    Authorization closed;
    closed.begin(0);
    assert(closed.enroll(owner, kEnrollmentWindowMs) == Decision::EnrollmentClosed);

    Authorization restored;
    restored.begin(100);
    Peer persisted = owner;
    persisted.encrypted = true;
    persisted.authenticated = true;
    persisted.bonded = true;
    restored.restore(persisted, static_cast<uint8_t>(Diagnostics | 0x80), 41);
    assert(!restored.ownerRecord().encrypted);
    assert(!restored.ownerRecord().authenticated);
    assert(!restored.ownerRecord().bonded);
    assert(restored.permissions() == static_cast<uint8_t>(Diagnostics | Admin));
    assert(restored.generation() == 41);
    assert(restored.authorize(owner, Diagnostics, request(10, 200, 1200, 41), 300) ==
           Decision::Allowed);

    Authorization tombstone;
    tombstone.begin(0);
    tombstone.restoreRevoked(9);
    assert(!tombstone.status(10).enrolled);
    assert(!tombstone.status(10).enrollmentOpen);
    assert(tombstone.generation() == 9);

    Authorization replacement;
    replacement.begin(0);
    assert(replacement.enroll(owner, 100) == Decision::Allowed);
    assert(replacement.beginReplacement(owner, request(20, 200, 1200), 300) ==
           Decision::Allowed);
    assert(!replacement.status(300).enrolled);
    assert(replacement.status(300).enrollmentOpen);
    assert(replacement.enroll(stranger, 400) == Decision::Allowed);
    assert(replacement.status(400).fingerprint == fingerprint(stranger));
    assert(!replacement.status(400).enrollmentOpen);

    // Device-uptime windows remain valid across uint32_t millis wraparound.
    Authorization wrapping;
    wrapping.begin(0xfffffff0U);
    assert(wrapping.enroll(owner, 0xfffffff5U) == Decision::Allowed);
    assert(wrapping.authorize(owner, Diagnostics,
                              request(11, 0xfffffff8U, 0x00000020U),
                              0x00000005U) == Decision::Allowed);

    assert(strcmp(decisionName(Decision::Replay), "replay") == 0);
    return 0;
}
