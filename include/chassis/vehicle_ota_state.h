#pragma once

#include <cstdint>

namespace Chassis {

// Debounces GTW_carState (0x318) byte 6. The low two bits encode the update
// state, but the remaining bits include a rolling value. We therefore require
// the complete byte to stay unchanged before asserting an OTA inhibit.
class VehicleOtaState
{
public:
    static constexpr uint8_t kInstalling = 2;
    static constexpr uint8_t kAssertStableSamples = 3;
    static constexpr uint8_t kClearSamples = 6;

    bool observe(uint8_t byte6)
    {
        const bool installing = (byte6 & 0x03U) == kInstalling;
        const bool sameFullByte = seen_ && byte6 == lastByte6_;
        lastByte6_ = byte6;
        seen_ = true;

        if (installing)
        {
            clearCount_ = 0;
            if (!sameFullByte)
                assertCount_ = 1;
            else if (assertCount_ < kAssertStableSamples)
                ++assertCount_;
            if (assertCount_ >= kAssertStableSamples)
                inProgress_ = true;
        }
        else
        {
            assertCount_ = 0;
            // Clear only on explicit consecutive non-installing samples.
            // A changing full byte is allowed here because the upper bits can
            // roll during normal operation.
            if (clearCount_ < kClearSamples)
                ++clearCount_;
            if (clearCount_ >= kClearSamples)
                inProgress_ = false;
        }
        return inProgress_;
    }

    void reset()
    {
        seen_ = false;
        inProgress_ = false;
        lastByte6_ = 0;
        assertCount_ = 0;
        clearCount_ = 0;
    }

    bool seen() const { return seen_; }
    bool inProgress() const { return inProgress_; }
    uint8_t lastByte6() const { return lastByte6_; }

private:
    bool seen_ = false;
    bool inProgress_ = false;
    uint8_t lastByte6_ = 0;
    uint8_t assertCount_ = 0;
    uint8_t clearCount_ = 0;
};

} // namespace Chassis
