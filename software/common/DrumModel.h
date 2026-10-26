// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <istream>

namespace mimic
{
inline constexpr std::size_t drumWindowFrames = 32;
inline constexpr std::size_t drumFeatureCount = drumWindowFrames * 2 + 5;
inline constexpr std::size_t drumClassCount = 3;
using DrumWaveform = std::array<std::array<float, drumWindowFrames>, 2>;
using DrumFeatures = std::array<float, drumFeatureCount>;

// Channels are head then rim, with their measured quiet baselines removed.
// Single-zone inputs use zeros for the absent rim channel.
// This pilot representation retains the waveform and relative channel level.
// Acquisition, onset alignment and rejection of corrupt/clipped data are separate.
DrumFeatures drumFeatures(const DrumWaveform& waveform, bool secondaryPresent = true);

struct DrumPrediction
{
    // Class order: background, head, rim, rimshot. Legacy models leave rimshot zero. Probabilities
    // are not calibrated confidence guarantees and must not directly authorize a MIDI event.
    std::array<float, 4> probabilities{1.0f, 0.0f, 0.0f, 0.0f};
    std::size_t classIndex = 0;
    bool valid = false;
};

// A deliberately small supervised pilot, not a validated performance trigger.
// Loading belongs off the real-time thread; prediction allocates no memory.
class DrumModel final
{
public:
    static DrumModel load(std::istream& input);
    [[nodiscard]] DrumPrediction predict(const DrumFeatures& features) const noexcept;

    [[nodiscard]] std::size_t zoneCount() const noexcept
    {
        return zones;
    }

    [[nodiscard]] bool supportsZoneMask() const noexcept
    {
        return masked;
    }

    [[nodiscard]] std::size_t classCount() const noexcept
    {
        return activeClasses;
    }

private:
    DrumModel() = default;
    bool masked = false;
    std::size_t zones = 2;
    std::size_t activeClasses = drumClassCount;
    DrumFeatures means{};
    DrumFeatures scales{};
    std::array<DrumFeatures, drumClassCount> weights{};
    std::array<float, drumClassCount> biases{};
};
} // namespace mimic
