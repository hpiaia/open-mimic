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
// Minimum class probability before the classifier may override the zone rule.
// Index 0 (background) is unused. The former fixed 0.65 is the default.
using ClassGate = std::array<float, 4>;
inline constexpr ClassGate defaultClassGate{0.65f, 0.65f, 0.65f, 0.65f};

struct TriggerPad
{
    using Predictor = std::function<DrumPrediction(const DrumFeatures&)>;
    int head = -1;
    int rim = -1;
    int zones = 2;
    PadResponse response;
    // Optional pad-specific classifier (for example a calibration fine-tune).
    // Empty means the engine's shared predictor is used.
    Predictor predictor;
    ClassGate gate = defaultClassGate;
    TriggerSettings settings;
};

struct TriggerEvent
{
    std::size_t pad = 0;
    unsigned zone = 0;
    unsigned velocity = 0;
    float probability = 0.0f;
    float peak = 0.0f;
    std::array<float, 2> sensorPeaks{};
    // Zone the calibrated rule chose before any classifier opinion.
    unsigned ruleZone = 0;
    // True when the classifier, not the rule, produced the emitted zone.
    bool modelDecided = false;
};

// Two-stage event policy. Stage one (detection, timing, velocity) is
// conventional: measured thresholds, a decaying envelope of the previous hit's
// peak with retrigger cancel, and a mask time. Stage two (head / rim / rimshot) applies the
// calibrated zone rule and lets the waveform classifier settle ambiguous cases.
// A detected hit is never discarded by the classifier.
class TriggerEngine final
{
public:
    using Predictor = TriggerPad::Predictor;
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

    [[nodiscard]] std::uint64_t modelDecisions() const noexcept
    {
        return modelDecided;
    }

    // Effective zone rule after user balance / rimshot overrides, for display.
    [[nodiscard]] ZoneRule effectiveRule(std::size_t pad) const;

private:
    struct PadState
    {
        TriggerPad input;
        ZoneRule rule;
        std::array<float, 2> decayFactor{};
        float retriggerFactor = 1.0f;
        unsigned maskFrames = 0;
        unsigned followFrames = 0;
        unsigned sinceHit = 0;
        std::array<float, 2> baseline{};
        std::array<float, 2> onsetBaseline{};
        std::array<float, 2> envelope{};
        DrumWaveform waveform{};
        std::size_t filled = 0;
        unsigned warmup = 0;
        unsigned mask = 0;
        unsigned quietFrames = 0;
        unsigned startupUnsettled = 0;
        float noise = 1.0f;
        std::array<float, 2> sensorNoise{1.0f, 1.0f};
        std::array<float, 2> sensorPeaks{};
        float peak = 0.0f;
        bool pending = false;
        bool initialQuietFound = false;
    };

    static unsigned ruleZone(const PadState& state, bool& ambiguous);
    unsigned decideZone(const PadState& state,
                        bool dual,
                        DrumPrediction& prediction,
                        bool& usedModel,
                        unsigned& ruled);

    Predictor predictor;
    std::vector<PadState> states;
    std::array<TriggerEvent, 32> events{};
    std::size_t count = 0;
    std::uint64_t rejected = 0;
    std::uint64_t clipped = 0;
    std::uint64_t modelDecided = 0;
};
} // namespace mimic
