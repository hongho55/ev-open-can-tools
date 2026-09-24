#pragma once

#include <stdint.h>
#include "can_frame_types.h"
#include "chassis/vehicle_ota_state.h"

namespace Chassis
{

enum class GlobalTxSafetyDecision : uint8_t
{
    Allowed = 0,
    VehicleOta,
    AutoparkOrDiStale,
};

inline bool globalTxAutoparkState(uint8_t state)
{
    switch (state)
    {
    case 3: // SUMMON
    case 4: // AUTOPARK
    case 9: // SMART_SUMMON
        return true;
    default:
        return false;
    }
}

class GlobalTxSafetyState
{
public:
    explicit GlobalTxSafetyState(uint32_t diTimeoutMs = 1500)
        : diTimeoutMs_(diTimeoutMs)
    {
    }

    bool observe(const CanFrame &frame, uint32_t nowMs)
    {
        advanceClock(nowMs);
        if (!trustedVehicleBus(frame))
            return false;

        if (frame.id == 0x286)
        {
            if (frame.dlc < 4)
                return false;
            diState_ = static_cast<uint8_t>((frame.data[3] >> 1) & 0x0F);
            diLastMs_ = nowMs;
            diSeen_ = true;
            return true;
        }

        if (frame.id == 0x318)
        {
            if (frame.dlc < 7)
                return false;
            vehicleOta_.observe(frame.data[6]);
            return true;
        }

        return false;
    }

    void advanceClock(uint32_t nowMs)
    {
        if (clockSeen_ && nowMs < lastClockMs_)
            diSeen_ = false;
        lastClockMs_ = nowMs;
        clockSeen_ = true;
    }

    GlobalTxSafetyDecision decision(uint32_t nowMs)
    {
        advanceClock(nowMs);
        if (vehicleOta_.inProgress())
            return GlobalTxSafetyDecision::VehicleOta;
        if (!validTimeout() || !diSeen_ ||
            static_cast<uint32_t>(nowMs - diLastMs_) >= diTimeoutMs_ ||
            globalTxAutoparkState(diState_))
            return GlobalTxSafetyDecision::AutoparkOrDiStale;
        return GlobalTxSafetyDecision::Allowed;
    }

    void reset()
    {
        vehicleOta_.reset();
        diState_ = 0;
        diLastMs_ = 0;
        diSeen_ = false;
        lastClockMs_ = 0;
        clockSeen_ = false;
    }

private:
    static bool trustedVehicleBus(const CanFrame &frame)
    {
        if (frame.physicalBus != CAN_BUS_CAN_B)
            return false;
        const uint8_t trusted = CAN_BUS_CAN_B | CAN_BUS_CH | CAN_BUS_VEH;
        return (frame.bus & trusted) != 0;
    }

    bool validTimeout() const
    {
        return diTimeoutMs_ > 0 && diTimeoutMs_ < 0x80000000U;
    }

    VehicleOtaState vehicleOta_;
    uint32_t diTimeoutMs_ = 1500;
    uint32_t diLastMs_ = 0;
    uint8_t diState_ = 0;
    bool diSeen_ = false;
    uint32_t lastClockMs_ = 0;
    bool clockSeen_ = false;
};

} // namespace Chassis
