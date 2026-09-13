#pragma once

#include <stddef.h>
#include <stdint.h>

namespace CanBitrate
{

enum class Confidence : uint8_t { None, Low, Medium, High };

struct Evidence
{
    uint32_t bitrate = 0;
    uint32_t validFrames = 0;
    uint32_t implausibleFrames = 0;
    uint32_t controllerErrors = 0;
    uint32_t timestampRegressions = 0;
    bool listenOnly = false;
    bool stableWindow = false;
};

struct RankedCandidate
{
    uint32_t bitrate = 0;
    int32_t score = 0;
    bool validProbe = false;
};

struct Recommendation
{
    RankedCandidate ranked[4] = {};
    uint8_t count = 0;
    uint32_t candidateBitrate = 0;
    Confidence confidence = Confidence::None;
    bool requiresExplicitConfirmation = true;
    bool mayApplyAutomatically = false;
};

inline bool supportedRate(uint32_t bitrate)
{
    return bitrate == 125000 || bitrate == 250000 ||
           bitrate == 500000 || bitrate == 1000000;
}

inline RankedCandidate score(const Evidence &evidence)
{
    RankedCandidate result;
    result.bitrate = evidence.bitrate;
    result.validProbe = evidence.listenOnly && supportedRate(evidence.bitrate);
    if (!result.validProbe)
    {
        result.score = -1000000000;
        return result;
    }

    const uint32_t boundedValid = evidence.validFrames > 100000
                                      ? 100000
                                      : evidence.validFrames;
    int64_t value = int64_t(boundedValid) * 2;
    value -= int64_t(evidence.implausibleFrames) * 10;
    value -= int64_t(evidence.controllerErrors) * 100;
    value -= int64_t(evidence.timestampRegressions) * 50;
    if (evidence.stableWindow && evidence.validFrames >= 100 &&
        evidence.controllerErrors == 0)
        value += 500;
    if (value > 1000000000)
        value = 1000000000;
    if (value < -1000000000)
        value = -1000000000;
    result.score = int32_t(value);
    return result;
}

inline Recommendation recommend(const Evidence *evidence, size_t count)
{
    Recommendation result;
    if (!evidence || count == 0)
        return result;
    result.count = uint8_t(count > 4 ? 4 : count);
    for (uint8_t i = 0; i < result.count; i++)
        result.ranked[i] = score(evidence[i]);

    for (uint8_t i = 0; i < result.count; i++)
    {
        for (uint8_t j = uint8_t(i + 1); j < result.count; j++)
        {
            if (result.ranked[j].score > result.ranked[i].score)
            {
                RankedCandidate swap = result.ranked[i];
                result.ranked[i] = result.ranked[j];
                result.ranked[j] = swap;
            }
        }
    }

    if (!result.ranked[0].validProbe)
        return result;
    result.candidateBitrate = result.ranked[0].bitrate;
    const int32_t margin = result.count > 1
                               ? result.ranked[0].score - result.ranked[1].score
                               : result.ranked[0].score;

    const Evidence *winner = nullptr;
    for (uint8_t i = 0; i < result.count; i++)
    {
        if (evidence[i].bitrate == result.candidateBitrate && evidence[i].listenOnly)
        {
            winner = &evidence[i];
            break;
        }
    }
    if (!winner)
        return result;

    if (winner->validFrames >= 100 && winner->controllerErrors == 0 &&
        winner->stableWindow && margin >= 500)
        result.confidence = Confidence::High;
    else if (winner->validFrames >= 20 && winner->controllerErrors <= 1 &&
             winner->stableWindow && margin >= 100)
        result.confidence = Confidence::Medium;
    else
        result.confidence = Confidence::Low;
    return result;
}

inline bool confidenceAllowsConfirmation(Confidence confidence)
{
    return confidence == Confidence::Medium || confidence == Confidence::High;
}

} // namespace CanBitrate
