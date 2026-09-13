#pragma once

#include <cstddef>
#include <cstdint>

namespace Chassis
{
namespace DecoderRegistry
{
inline constexpr const char *kSchemaVersion = "t2can-decoder-registry-v1";

enum class Bus : uint8_t { Chassis, Party, Vehicle };
enum class Confidence : uint8_t { Observed, Inferred, Confirmed };
enum class Use : uint8_t { DisplayOnly, PolicyGate, TxGeneration };

struct Definition
{
    uint32_t id;
    Bus bus;
    uint8_t minDlc;
    int8_t mux;
    const char *signal;
    const char *extraction;
    float scale;
    float offset;
    bool signedValue;
    uint32_t freshnessMs;
    const char *profile;
    const char *source;
    const char *evidence;
    Confidence confidence;
    Use use;
};

// Metadata for signals already decoded by TelemetryState. This registry is
// descriptive only: adding an entry never enables a policy gate or TX path.
inline constexpr Definition kDefinitions[] = {
    {0x118, Bus::Chassis, 7, -1, "gear", "byte2 bits5..7", 1.0f, 0.0f, false, 1500, "legacy/hw3/hw4", "repository signal mapping", "native fixtures", Confidence::Confirmed, Use::PolicyGate},
    {0x118, Bus::Chassis, 7, -1, "autonomyActive", "byte6 bit2", 1.0f, 0.0f, false, 1500, "legacy/hw3/hw4", "repository signal mapping", "native fixtures", Confidence::Confirmed, Use::PolicyGate},
    {0x129, Bus::Chassis, 4, -1, "steeringAngleDeg", "byte2 + byte3 bits0..5", 0.1f, -819.2f, false, 1500, "legacy/hw3/hw4", "repository signal mapping", "native fixtures", Confidence::Confirmed, Use::DisplayOnly},
    {0x145, Bus::Chassis, 4, -1, "driverBrakeApplied", "bit29", 1.0f, 0.0f, false, 1500, "legacy/hw3/hw4", "repository signal mapping", "native fixtures", Confidence::Confirmed, Use::PolicyGate},
    {0x238, Bus::Chassis, 2, -1, "mapPresence", "frame presence", 1.0f, 0.0f, false, 1500, "legacy/hw3/hw4", "repository observation", "receive fixtures", Confidence::Observed, Use::DisplayOnly},
    {0x257, Bus::Chassis, 3, -1, "speedKph", "byte1 bits4..7 + byte2", 0.08f, -40.0f, false, 1500, "legacy/hw3/hw4", "repository signal mapping", "native fixtures", Confidence::Confirmed, Use::PolicyGate},
    {0x293, Bus::Chassis, 5, -1, "autosteerEnabled", "bit38", 1.0f, 0.0f, false, 1500, "hw3/hw4", "repository signal mapping", "native fixtures", Confidence::Confirmed, Use::DisplayOnly},
    {0x2B9, Bus::Chassis, 3, -1, "accState", "bit12 length4", 1.0f, 0.0f, false, 1500, "hw3/hw4", "repository signal mapping", "native fixtures", Confidence::Confirmed, Use::DisplayOnly},
    {0x389, Bus::Chassis, 5, -1, "accReport", "bit26 length5", 1.0f, 0.0f, false, 1500, "hw3/hw4", "repository signal mapping", "native fixtures", Confidence::Confirmed, Use::DisplayOnly},
    {0x399, Bus::Chassis, 8, -1, "dasStatus", "layout-specific fields", 1.0f, 0.0f, false, 1500, "legacy/hw3", "repository signal mapping", "layout fixtures", Confidence::Confirmed, Use::DisplayOnly},
    {0x39B, Bus::Chassis, 8, -1, "dasStatus", "layout-specific fields", 1.0f, 0.0f, false, 1500, "standard/highland hw4", "repository signal mapping", "layout fixtures", Confidence::Confirmed, Use::DisplayOnly},
    {0x7FF, Bus::Chassis, 6, 2, "vehicleTier", "byte5 bits2..4", 1.0f, 0.0f, false, 1500, "legacy/hw3/hw4", "repository signal mapping", "mux fixtures", Confidence::Confirmed, Use::DisplayOnly},
    {0x229, Bus::Vehicle, 3, -1, "rightStalkCrc", "byte0", 1.0f, 0.0f, false, 1500, "model3/y vehicle-can", "opendbc tesla_model3_vehicle.dbc", "2328 RX frames; algorithm unverified", Confidence::Observed, Use::DisplayOnly},
    {0x229, Bus::Vehicle, 3, -1, "rightStalkCounter", "byte1 bits0..3", 1.0f, 0.0f, false, 1500, "model3/y vehicle-can", "opendbc tesla_model3_vehicle.dbc", "2328 RX frames across 38 files", Confidence::Confirmed, Use::DisplayOnly},
    {0x229, Bus::Vehicle, 3, -1, "rightStalkStatus", "byte1 bits4..6", 1.0f, 0.0f, false, 1500, "model3/y vehicle-can", "opendbc tesla_model3_vehicle.dbc", "one non-idle observation", Confidence::Observed, Use::DisplayOnly},
    {0x229, Bus::Vehicle, 3, -1, "parkButtonStatus", "byte2 bits0..1", 1.0f, 0.0f, false, 1500, "model3/y vehicle-can", "opendbc tesla_model3_vehicle.dbc", "unexercised in research set", Confidence::Inferred, Use::DisplayOnly},
    {0x132, Bus::Party, 4, -1, "packVoltageV", "little-endian bytes0..1", 0.01f, 0.0f, false, 1500, "party-can", "upstream mapping", "native fixtures", Confidence::Inferred, Use::DisplayOnly},
    {0x132, Bus::Party, 4, -1, "packCurrentA", "signed little-endian bytes2..3", 0.1f, 0.0f, true, 1500, "party-can", "upstream mapping", "native fixtures", Confidence::Inferred, Use::DisplayOnly},
    {0x292, Bus::Party, 3, -1, "socPercent", "byte1 bits2..7 + byte2", 0.1f, 0.0f, false, 1500, "party-can", "upstream mapping", "native fixtures", Confidence::Inferred, Use::DisplayOnly},
    {0x312, Bus::Party, 6, -1, "batteryTemperature", "bytes4..5", 1.0f, -40.0f, true, 1500, "party-can", "upstream mapping", "native fixtures", Confidence::Inferred, Use::DisplayOnly},
};

inline constexpr size_t size() { return sizeof(kDefinitions) / sizeof(kDefinitions[0]); }

inline const Definition *find(uint32_t id, Bus bus, const char *signal)
{
    if (!signal) return nullptr;
    for (const Definition &definition : kDefinitions)
    {
        if (definition.id != id || definition.bus != bus) continue;
        const char *left = definition.signal;
        const char *right = signal;
        while (*left && *left == *right) { ++left; ++right; }
        if (*left == '\0' && *right == '\0') return &definition;
    }
    return nullptr;
}

inline constexpr const char *confidenceName(Confidence value)
{
    return value == Confidence::Confirmed ? "confirmed"
           : value == Confidence::Inferred ? "inferred"
                                           : "observed";
}

inline constexpr const char *useName(Use value)
{
    return value == Use::PolicyGate ? "policy_gate"
           : value == Use::TxGeneration ? "tx_generation"
                                        : "display_only";
}
} // namespace DecoderRegistry
} // namespace Chassis
