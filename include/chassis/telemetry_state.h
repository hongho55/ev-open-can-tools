#pragma once

#include <cstdint>

#include "../can_helpers.h"
#include "signals.h"

namespace Chassis
{
// Read-only dashboard snapshot.  This deliberately stores only signals whose
// layouts are documented in this repository.  Unknown IDs, buses, layouts and
// stale samples remain unavailable instead of being guessed.
struct TelemetrySnapshot
{
    uint32_t acceptedFrames = 0;
    uint32_t lastObservedMs = 0;

    bool speedSeen = false;
    float speedKph = 0.0f;
    uint8_t displaySpeed = 0;
    uint32_t speedMs = 0;

    bool gearSeen = false;
    uint8_t gear = 0;
    bool autonomyActive = false;
    uint32_t gearMs = 0;

    bool steeringSeen = false;
    float steeringAngleDeg = 0.0f;
    uint32_t steeringMs = 0;

    bool brakeSeen = false;
    bool brakeApplied = false;
    uint32_t brakeMs = 0;

    bool dasSeen = false;
    uint8_t apState = 0;
    uint8_t handsOn = 0;
    uint32_t dasMs = 0;
    uint8_t laneChange = 0, sideWarning = 0, sideCollisionAvoid = 0;
    uint8_t laneDepartureWarning = 0, forwardWarning = 0;
    bool visionLimitSeen = false;
    uint16_t visionLimitKph = 0;

    bool dasStatus2Seen = false;
    // Last decoded ACC value for the existing aggregate dashboard field. Use
    // dasControlSeen/accState to distinguish DAS_control from DAS_status2.
    uint8_t accReport = 0;
    uint8_t activationFailureStatus = 0;
    uint32_t dasStatus2Ms = 0;

    bool dasControlSeen = false;
    uint8_t accState = 0;
    uint32_t dasControlMs = 0;

    bool dasSettingsSeen = false;
    bool autosteerEnabled = false;
    uint32_t dasSettingsMs = 0;

    bool diModesSeen = false;
    uint8_t trackModeState = 0;
    uint8_t tractionControlMode = 0;
    uint32_t diModesMs = 0;

    bool apLegacySeen = false;
    bool apControlSeen = false;
    bool dasSteeringSeen = false;
    bool mapSeen = false;

    // Party-CAN BMS values and presence-only diagnostics use independent clocks.
    bool bmsHvSeen = false;
    bool bmsSocSeen = false;
    bool bmsThermalSeen = false;
    bool energySeen = false;
    bool torqueSeen = false;
    bool diStateSeen = false;
    bool warningSeen = false;
    uint32_t partyLastMs = 0;

    float packVoltageV = 0, packCurrentA = 0, socPercent = 0;
    int16_t tempMinC = 0, tempMaxC = 0;
    uint32_t bmsHvMs = 0, bmsSocMs = 0, bmsThermalMs = 0;
    uint32_t energyMs = 0, torqueMs = 0, diStateMs = 0, warningMs = 0;
    uint32_t apLegacyMs = 0, apControlMs = 0, dasSteeringMs = 0, mapMs = 0;

    bool tierSeen = false;
    uint8_t tier = 0;
    uint32_t tierMs = 0;
};

class TelemetryState
{
public:
    explicit TelemetryState(DasLayout layout = DasLayout::Unknown,
                            uint32_t timeoutMs = 1500)
        : layout_(layout), timeoutMs_(timeoutMs) {}

    void setLayout(DasLayout layout)
    {
        if (layout_ == layout) return;
        layout_ = layout;
        reset();
    }

    void setTimeout(uint32_t timeoutMs) { timeoutMs_ = timeoutMs; }

    bool observe(const CanFrame &frame, uint32_t nowMs)
    {
        if (frame.id > 0x7FF || frame.dlc > 8 || frame.bus == CAN_BUS_ANY)
            return false;

        if (observeChassis(frame, nowMs)) return true;
        return observeParty(frame, nowMs);
    }

    TelemetrySnapshot snapshot(uint32_t nowMs) const
    {
        TelemetrySnapshot out = snapshot_;
        out.speedSeen = fresh(snapshot_.speedSeen, snapshot_.speedMs, nowMs);
        out.gearSeen = fresh(snapshot_.gearSeen, snapshot_.gearMs, nowMs);
        out.steeringSeen = fresh(snapshot_.steeringSeen, snapshot_.steeringMs, nowMs);
        out.brakeSeen = fresh(snapshot_.brakeSeen, snapshot_.brakeMs, nowMs);
        out.dasSeen = fresh(snapshot_.dasSeen, snapshot_.dasMs, nowMs);
        out.dasStatus2Seen = fresh(snapshot_.dasStatus2Seen,
                                   snapshot_.dasStatus2Ms, nowMs);
        out.dasControlSeen = fresh(snapshot_.dasControlSeen,
                                   snapshot_.dasControlMs, nowMs);
        out.dasSettingsSeen = fresh(snapshot_.dasSettingsSeen,
                                    snapshot_.dasSettingsMs, nowMs);
        out.diModesSeen = fresh(snapshot_.diModesSeen, snapshot_.diModesMs, nowMs);
        out.apLegacySeen = fresh(snapshot_.apLegacySeen, snapshot_.apLegacyMs, nowMs);
        out.apControlSeen = fresh(snapshot_.apControlSeen, snapshot_.apControlMs, nowMs);
        out.dasSteeringSeen = fresh(snapshot_.dasSteeringSeen, snapshot_.dasSteeringMs, nowMs);
        out.mapSeen = fresh(snapshot_.mapSeen, snapshot_.mapMs, nowMs);
        out.bmsHvSeen = fresh(snapshot_.bmsHvSeen, snapshot_.bmsHvMs, nowMs);
        out.bmsSocSeen = fresh(snapshot_.bmsSocSeen, snapshot_.bmsSocMs, nowMs);
        out.bmsThermalSeen = fresh(snapshot_.bmsThermalSeen, snapshot_.bmsThermalMs, nowMs);
        out.energySeen = fresh(snapshot_.energySeen, snapshot_.energyMs, nowMs);
        out.torqueSeen = fresh(snapshot_.torqueSeen, snapshot_.torqueMs, nowMs);
        out.diStateSeen = fresh(snapshot_.diStateSeen, snapshot_.diStateMs, nowMs);
        out.warningSeen = fresh(snapshot_.warningSeen, snapshot_.warningMs, nowMs);
        out.visionLimitSeen = snapshot_.visionLimitSeen && out.dasSeen;
        out.tierSeen = fresh(snapshot_.tierSeen, snapshot_.tierMs, nowMs);
        return out;
    }

    void reset() { snapshot_ = {}; }

private:
    static bool chassisBus(uint8_t bus)
    {
        constexpr uint8_t accepted = CAN_BUS_CH | CAN_BUS_VEH | CAN_BUS_CAN_B;
        return (bus & accepted) != 0 && (bus & ~accepted) == 0;
    }

    static bool partyBus(uint8_t bus)
    {
        constexpr uint8_t accepted = CAN_BUS_PARTY | CAN_BUS_CAN_A;
        return (bus & accepted) != 0 && (bus & ~accepted) == 0;
    }

    bool fresh(bool seen, uint32_t sampleMs, uint32_t nowMs) const
    {
        return seen && timeoutMs_ > 0 && timeoutMs_ < 0x80000000u &&
               uint32_t(nowMs - sampleMs) < timeoutMs_;
    }

    static bool hasDlc(const CanFrame &frame, uint8_t minDlc)
    {
        return frame.dlc >= minDlc && frame.dlc <= 8;
    }

    static uint16_t speedRaw(const CanFrame &frame)
    {
        return static_cast<uint16_t>((frame.data[1] >> 4) |
                                     (static_cast<uint16_t>(frame.data[2]) << 4));
    }

    bool observeChassis(const CanFrame &frame, uint32_t nowMs)
    {
        if (!chassisBus(frame.bus)) return false;

        switch (frame.id)
        {
        case 0x118: // DI_systemStatus: gear + autonomy-control-active
            if (!hasDlc(frame, 7)) return false;
            snapshot_.gearSeen = true;
            snapshot_.gear = static_cast<uint8_t>((frame.data[2] >> 5) & 0x07);
            snapshot_.autonomyActive = (frame.data[6] & 0x04) != 0;
            snapshot_.trackModeState = frame.data[kDiTrackModeByte] & kDiTrackModeMask;
            snapshot_.tractionControlMode = frame.data[kDiTractionControlByte] & kDiTractionControlMask;
            snapshot_.diModesSeen = true;
            snapshot_.diModesMs = nowMs;
            snapshot_.gearMs = nowMs;
            break;
        case 0x129: // SCCM_steeringAngleSensor
            if (!hasDlc(frame, 4) || ((frame.data[3] >> 6) & 0x03) != 1)
                return false;
            snapshot_.steeringSeen = true;
            snapshot_.steeringAngleDeg =
                static_cast<float>(static_cast<uint16_t>(frame.data[2]) |
                                   (static_cast<uint16_t>(frame.data[3] & 0x3F) << 8)) *
                    0.1f - 819.2f;
            snapshot_.steeringMs = nowMs;
            break;
        case 0x145: // ESP_status: driver brake apply, bit 29
            if (!hasDlc(frame, 4)) return false;
            snapshot_.brakeSeen = true;
            snapshot_.brakeApplied = isESPDriverBrakeApplied(frame);
            snapshot_.brakeMs = nowMs;
            break;
        case 0x238: // UI_driverAssistMapData: presence only
            if (!hasDlc(frame, 2)) return false;
            snapshot_.mapSeen = true;
            snapshot_.mapMs = nowMs;
            break;
        case 0x257: // DI_speed
            if (!hasDlc(frame, 3)) return false;
            {
                uint16_t raw = speedRaw(frame);
                if (raw == 0x0FFF) return false; // SNA
                snapshot_.speedSeen = true;
                snapshot_.speedKph = static_cast<float>(raw) * 0.08f - 40.0f;
                snapshot_.displaySpeed = frame.dlc >= 4 ? frame.data[3] : 0;
                snapshot_.speedMs = nowMs;
            }
            break;
        case 0x2B9: // DAS_control: DAS_accState, bit 12|4
            if (!hasDlc(frame, 3)) return false;
            snapshot_.dasControlSeen = true;
            snapshot_.accState = readDASControlAccState(frame);
            snapshot_.accReport = snapshot_.accState;
            snapshot_.dasControlMs = nowMs;
            break;
        case 0x389: // DAS_status2: DAS_ACC_report, bit 26|5
            if (!hasDlc(frame, 5)) return false;
            snapshot_.dasStatus2Seen = true;
            snapshot_.accReport = readDASStatus2AccReport(frame);
            snapshot_.activationFailureStatus = readDASStatus2ActivationFailure(frame);
            snapshot_.dasStatus2Ms = nowMs;
            break;
        case 0x293: // DAS_settings: DAS_autosteerEnabled, bit 38|1
            if (!hasDlc(frame, 5)) return false;
            snapshot_.dasSettingsSeen = true;
            snapshot_.autosteerEnabled =
                ((frame.data[kDasSettingsAutosteerByte] >> kDasSettingsAutosteerShift) &
                 kDasSettingsAutosteerMask) != 0;
            snapshot_.dasSettingsMs = nowMs;
            break;
        case 0x399: // Legacy/HW3 DAS layout only
            if (layout_ != DasLayout::LegacyHw3 || frame.dlc != 8) return false;
            snapshot_.dasSeen = true;
            snapshot_.apState = frame.data[0] & 0x0F;
            snapshot_.handsOn = (frame.data[5] >> 2) & 0x0F;
            decodeDas(frame);
            snapshot_.dasMs = nowMs;
            break;
        case 0x39B: // Standard or explicitly selected Highland byte-0 DAS layout
            if ((layout_ != DasLayout::StandardHw4 &&
                 layout_ != DasLayout::HighlandHw4Byte0) || frame.dlc != 8) return false;
            snapshot_.dasSeen = true;
            snapshot_.apState = readDASAutopilotStatus(frame, layout_);
            snapshot_.handsOn = (frame.data[5] >> 2) & 0x0F;
            decodeDas(frame);
            snapshot_.dasMs = nowMs;
            break;
        case 0x3EE:
            if (frame.dlc != 8) return false;
            snapshot_.apLegacySeen = true;
            snapshot_.apLegacyMs = nowMs;
            break;
        case 0x3FD:
            if (frame.dlc != 8) return false;
            snapshot_.apControlSeen = true;
            snapshot_.apControlMs = nowMs;
            break;
        case 0x488:
            if (!hasDlc(frame, 3)) return false;
            snapshot_.dasSteeringSeen = true;
            snapshot_.dasSteeringMs = nowMs;
            break;
        case 0x7FF: // GTW_carConfig mux 2, tier readback
            if (!hasDlc(frame, 6) || (frame.data[0] & 0x07) != 2) return false;
            snapshot_.tierSeen = true;
            snapshot_.tier = (frame.data[5] >> 2) & 0x07;
            snapshot_.tierMs = nowMs;
            break;
        default:
            return false;
        }
        ++snapshot_.acceptedFrames;
        snapshot_.lastObservedMs = nowMs;
        return true;
    }

    // Flipper fsd_logic/fsd_handler.c DAS bit fields; display raw warning enums.
    void decodeDas(const CanFrame &frame)
    {
        snapshot_.laneChange = ((frame.data[5] >> 6) & 3) | ((frame.data[6] & 7) << 2);
        snapshot_.sideWarning = frame.data[4] & 3;
        snapshot_.sideCollisionAvoid =
            (frame.data[kDasSideCollisionAvoidByte] >> kDasSideCollisionAvoidShift) &
            kDasSideCollisionAvoidMask;
        snapshot_.laneDepartureWarning =
            (frame.data[kDasLaneDepartureByte] >> kDasLaneDepartureShift) &
            kDasLaneDepartureMask;
        snapshot_.forwardWarning = (frame.data[2] >> 6) & 3;
        const uint8_t limit = frame.data[2] & 31;
        snapshot_.visionLimitSeen = limit != 0 && limit != 31;
        snapshot_.visionLimitKph = limit * 5;
    }

    bool observeParty(const CanFrame &frame, uint32_t nowMs)
    {
        if (!partyBus(frame.bus)) return false;
        bool accepted = false;
        switch (frame.id)
        {
        // BMS scaling follows Flipper ESP32 can_signals.h. No TX is generated.
        case 0x132:
            if (!hasDlc(frame, 4)) return false;
            snapshot_.packVoltageV = (frame.data[0] | (uint16_t(frame.data[1]) << 8)) * 0.01f;
            {
                const uint16_t raw = frame.data[2] | (uint16_t(frame.data[3]) << 8);
                const int32_t signedRaw = raw >= 0x8000 ? int32_t(raw) - 65536 : raw;
                snapshot_.packCurrentA = signedRaw * 0.1f;
            }
            snapshot_.bmsHvSeen = accepted = true;
            snapshot_.bmsHvMs = nowMs;
            break;
        case 0x292:
            if (!hasDlc(frame, 3)) return false;
            {
                const uint16_t raw = ((frame.data[1] >> 2) | (uint16_t(frame.data[2]) << 6)) & 1023;
                if (raw > 1000) return false;
                snapshot_.socPercent = raw * 0.1f;
            }
            snapshot_.bmsSocSeen = accepted = true;
            snapshot_.bmsSocMs = nowMs;
            break;
        case 0x312:
            if (!hasDlc(frame, 6) || frame.data[4] == 255 || frame.data[5] == 255 ||
                frame.data[4] > frame.data[5]) return false;
            snapshot_.tempMinC = int16_t(frame.data[4]) - 40;
            snapshot_.tempMaxC = int16_t(frame.data[5]) - 40;
            snapshot_.bmsThermalSeen = accepted = true;
            snapshot_.bmsThermalMs = nowMs;
            break;
        case 0x108:
            if (!hasDlc(frame, 2)) return false;
            snapshot_.torqueSeen = accepted = true;
            snapshot_.torqueMs = nowMs;
            break;
        case 0x286:
            if (!hasDlc(frame, 1)) return false;
            snapshot_.diStateSeen = accepted = true;
            snapshot_.diStateMs = nowMs;
            break;
        case 0x311:
            if (!hasDlc(frame, 1)) return false;
            snapshot_.warningSeen = accepted = true;
            snapshot_.warningMs = nowMs;
            break;
        case 0x33A:
            if (!hasDlc(frame, 1)) return false;
            snapshot_.energySeen = accepted = true;
            snapshot_.energyMs = nowMs;
            break;
        case 0x175: accepted = hasDlc(frame, 1); break;
        default: return false;
        }
        if (!accepted) return false;
        snapshot_.partyLastMs = nowMs;
        snapshot_.lastObservedMs = nowMs;
        ++snapshot_.acceptedFrames;
        return true;
    }

    DasLayout layout_;
    uint32_t timeoutMs_;
    TelemetrySnapshot snapshot_{};
};
} // namespace Chassis
