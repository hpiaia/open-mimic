// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "DrumModel.h"
#include "PadCalibration.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace mimic
{
struct TriggerPad
{
    int head = -1;
    int rim = -1;
    int zones = 2;
    PadResponse response;
};

struct TriggerEvent
{
    std::size_t pad = 0;
    unsigned zone = 0;
    unsigned velocity = 0;
    float probability = 0.0f;
    float peak = 0.0f;
};

// Event policy around the waveform classifier. It is intentionally separate
// from the learned weights, physical channel routing and MIDI transport.
class TriggerEngine final
{
public:
    using Predictor = std::function<DrumPrediction(const DrumFeatures&)>;
    explicit TriggerEngine(Predictor predict);
    void configure(const std::vector<TriggerPad>& pads);
    void reset() noexcept;
    void beginBatch() noexcept;
    void processFrame(unsigned adc, const std::array<std::uint16_t, 16>& frame);

    [[nodiscard]] std::size_t eventCount() const noexcept
    {
        return count;
    }

    [[nodiscard]] const TriggerEvent& event(std::size_t index) const
    {
        return events.at(index);
    }

    [[nodiscard]] std::uint64_t rejectedCount() const noexcept
    {
        return rejected;
    }

    [[nodiscard]] std::uint64_t clippedCount() const noexcept
    {
        return clipped;
    }

private:
    struct PadState
    {
        TriggerPad input;
        std::array<float, 2> baseline{};
        std::array<float, 2> onsetBaseline{};
        DrumWaveform waveform{};
        std::size_t filled = 0;
        unsigned warmup = 0;
        unsigned quiet = 0;
        unsigned cooldown = 0;
        unsigned startupUnsettled = 0;
        float noise = 1.0f;
        std::array<float, 2> sensorNoise{1.0f, 1.0f};
        std::array<float, 2> sensorPeaks{};
        float peak = 0.0f;
        bool pending = false;
        bool armed = false;
        bool initialQuietFound = false;
    };

    Predictor predictor;
    std::vector<PadState> states;
    std::array<TriggerEvent, 32> events{};
    std::size_t count = 0;
    std::uint64_t rejected = 0;
    std::uint64_t clipped = 0;
};
} // namespace mimic
