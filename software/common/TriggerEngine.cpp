// SPDX-License-Identifier: AGPL-3.0-only
#include "TriggerEngine.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace mimic
{
namespace
{
// Relative distance to a rule boundary below which the classifier is consulted.
constexpr float ambiguityBand = 1.25f;

ZoneRule resolveRule(const TriggerPad& pad)
{
    ZoneRule rule = pad.response.calibrated ? pad.response.zone : ZoneRule{};
    if (!rule.calibrated)
    {
        rule = ZoneRule{};
    }
    // Balance shifts the whole ratio scale: positive favours head.
    const auto shift = std::exp2(pad.settings.balance / 100.0f);
    rule.headRatioCeiling *= shift;
    rule.rimRatioFloor *= shift;
    for (unsigned sensor = 0; sensor < 2; ++sensor)
    {
        if (pad.settings.rimshotThreshold[sensor] >= 0.0f)
        {
            (sensor == 0 ? rule.rimshotHead : rule.rimshotRim) =
                pad.settings.rimshotThreshold[sensor];
        }
        if (pad.settings.decayMs >= 0.0f)
        {
            rule.decayMs[sensor] = pad.settings.decayMs;
        }
    }
    return rule;
}

bool nearBoundary(float value, float boundary)
{
    return boundary > 0.0f && value > boundary / ambiguityBand && value < boundary * ambiguityBand;
}
} // namespace

TriggerEngine::TriggerEngine(Predictor predict) : predictor(std::move(predict)) {}

void TriggerEngine::configure(const std::vector<TriggerPad>& pads)
{
    if (pads.size() > 16)
    {
        throw std::invalid_argument("At most sixteen trigger pads are supported");
    }
    states.clear();
    states.reserve(pads.size());
    for (const auto& pad : pads)
    {
        if (pad.head < -1 || pad.head > 31 || pad.rim < -1 || pad.rim > 31
            || (pad.zones != 1 && pad.zones != 2))
        {
            throw std::invalid_argument("Invalid trigger input configuration");
        }
        if (pad.head >= 0 && pad.zones == 2 && pad.rim >= 0
            && (pad.head / 16 != pad.rim / 16 || pad.head == pad.rim))
        {
            throw std::invalid_argument(
                "Live dual-zone sensors require distinct channels on one ADC");
        }
        validatePadResponse(pad.response, pad.zones);
        validateTriggerSettings(pad.settings);
        for (std::size_t index = 1; index < pad.gate.size(); ++index)
        {
            if (!std::isfinite(pad.gate[index]) || pad.gate[index] < 0.0f || pad.gate[index] > 1.0f)
            {
                throw std::invalid_argument("Class gate thresholds must be within 0..1");
            }
        }
        PadState state;
        state.input = pad;
        state.rule = resolveRule(pad);
        for (unsigned sensor = 0; sensor < 2; ++sensor)
        {
            state.decayFactor[sensor] =
                std::exp(-1.0f / (state.rule.decayMs[sensor] * triggerFramesPerMs));
        }
        state.retriggerFactor = 1.05f + 1.95f * pad.settings.retriggerCancel / 100.0f;
        const auto follow =
            std::clamp(3.0f * std::max(state.rule.decayMs[0], state.rule.decayMs[1]), 15.0f, 60.0f);
        state.followFrames = static_cast<unsigned>(std::lround(follow * triggerFramesPerMs));
        state.maskFrames =
            static_cast<unsigned>(std::lround(pad.settings.maskMs * triggerFramesPerMs));
        states.push_back(state);
    }
    reset();
}

ZoneRule TriggerEngine::effectiveRule(std::size_t pad) const
{
    return states.at(pad).rule;
}

void TriggerEngine::reset() noexcept
{
    for (auto& state : states)
    {
        const auto input = state.input;
        const auto rule = state.rule;
        const auto decay = state.decayFactor;
        const auto retrigger = state.retriggerFactor;
        const auto mask = state.maskFrames;
        const auto follow = state.followFrames;
        state = {};
        state.input = input;
        state.rule = rule;
        state.decayFactor = decay;
        state.retriggerFactor = retrigger;
        state.maskFrames = mask;
        state.followFrames = follow;
        state.sinceHit = follow;
        if (input.response.calibrated)
        {
            for (unsigned sensor = 0; sensor < 2; ++sensor)
            {
                state.sensorNoise[sensor] = input.response.sensors[sensor].noiseMean;
            }
        }
    }
    count = 0;
}

void TriggerEngine::beginBatch() noexcept
{
    count = 0;
}

unsigned TriggerEngine::ruleZone(const PadState& state, bool& ambiguous)
{
    const auto& rule = state.rule;
    const auto headPeak = state.sensorPeaks[0];
    const auto rimPeak = state.sensorPeaks[1];
    const auto ratio = rimPeak / std::max(1.0f, headPeak);
    // Mimic Pro semantics: the ratio picks the struck zone; a rimshot is that
    // zone plus the other piezo above its rimshot level. Ratios between the
    // measured single-zone bands are ambiguous and may be settled by the model.
    const auto centre = std::sqrt(rule.headRatioCeiling * rule.rimRatioFloor);
    ambiguous = (ratio > rule.headRatioCeiling && ratio < rule.rimRatioFloor)
                || nearBoundary(ratio, rule.headRatioCeiling)
                || nearBoundary(ratio, rule.rimRatioFloor);
    if (ratio < centre)
    {
        ambiguous = ambiguous || nearBoundary(rimPeak, rule.rimshotRim);
        return rimPeak >= rule.rimshotRim ? 3U : 1U;
    }
    ambiguous = ambiguous || nearBoundary(headPeak, rule.rimshotHead);
    return headPeak >= rule.rimshotHead ? 3U : 2U;
}

unsigned TriggerEngine::decideZone(
    const PadState& state, bool dual, DrumPrediction& prediction, bool& usedModel, unsigned& ruled)
{
    usedModel = false;
    prediction = {};
    ruled = 1;
    if (!dual)
    {
        return 1;
    }
    bool ambiguous = false;
    ruled = ruleZone(state, ambiguous);
    const auto& pad = state.input;
    if (pad.settings.zoneMode == TriggerSettings::rule
        || (pad.settings.zoneMode == TriggerSettings::hybrid && !ambiguous))
    {
        return ruled;
    }
    prediction = pad.predictor ? pad.predictor(drumFeatures(state.waveform, dual))
                               : predictor(drumFeatures(state.waveform, dual));
    if (prediction.valid && prediction.classIndex >= 1 && prediction.classIndex <= 3
        && prediction.probabilities[prediction.classIndex] >= pad.gate[prediction.classIndex])
    {
        usedModel = true;
        return static_cast<unsigned>(prediction.classIndex);
    }
    return ruled;
}

void TriggerEngine::processFrame(unsigned adc, const std::array<std::uint16_t, 16>& frame)
{
    if (adc > 1)
    {
        throw std::invalid_argument("Invalid ADC index");
    }
    for (std::size_t index = 0; index < states.size(); ++index)
    {
        auto& state = states[index];
        const auto& pad = state.input;
        if (pad.head < 0 || static_cast<unsigned>(pad.head / 16) != adc
            || (pad.zones == 2 && pad.rim < 0))
        {
            continue;
        }
        const bool dual = pad.zones == 2;
        const unsigned sensors = dual ? 2U : 1U;
        const std::array<float, 2> raw{
            static_cast<float>(frame[static_cast<std::size_t>(pad.head % 16)] & 4095U),
            dual ? static_cast<float>(frame[static_cast<std::size_t>(pad.rim % 16)] & 4095U)
                 : 0.0f};
        if (state.mask > 0)
        {
            --state.mask;
        }
        if (raw[0] >= 4090.0f || (dual && raw[1] >= 4090.0f))
        {
            ++clipped;
            state.pending = false;
            state.mask = 128;
            state.envelope = {4095.0f, 4095.0f};
            continue;
        }
        if (state.warmup < 64)
        {
            if (pad.response.calibrated)
            {
                if (state.warmup == 0)
                {
                    for (unsigned sensor = 0; sensor < 2; ++sensor)
                    {
                        state.baseline[sensor] = pad.response.sensors[sensor].baseline;
                    }
                }
                for (unsigned sensor = 0; sensor < sensors; ++sensor)
                {
                    if (std::abs(raw[sensor] - state.baseline[sensor])
                        < pad.response.sensors[sensor].threshold)
                    {
                        state.baseline[sensor] += (raw[sensor] - state.baseline[sensor])
                                                  / static_cast<float>(state.warmup + 1);
                    }
                }
                ++state.warmup;
                continue;
            }
            if (state.warmup == 0)
            {
                state.baseline = raw;
            }
            else
            {
                for (std::size_t channel = 0; channel < 2; ++channel)
                {
                    state.baseline[channel] += (raw[channel] - state.baseline[channel])
                                               / static_cast<float>(state.warmup + 1);
                }
            }
            ++state.warmup;
            continue;
        }
        const std::array<float, 2> amplitudes{std::abs(raw[0] - state.baseline[0]),
                                              dual ? std::abs(raw[1] - state.baseline[1]) : 0.0f};
        const auto peak = std::max(amplitudes[0], amplitudes[1]);
        std::array<float, 2> thresholds{};
        bool quietInput = true;
        for (unsigned sensor = 0; sensor < sensors; ++sensor)
        {
            if (pad.response.calibrated)
            {
                const auto& response = pad.response.sensors[sensor];
                thresholds[sensor] =
                    response.threshold
                    * std::max(1.0f, state.sensorNoise[sensor] / response.noiseMean);
            }
            else
            {
                thresholds[sensor] = std::max(16.0f, 6.0f * state.noise + 8.0f);
            }
            thresholds[sensor] += pad.settings.threshold;
            quietInput = quietInput && amplitudes[sensor] < thresholds[sensor];
        }
        // An uncalibrated warm-up may begin in a strike/decay after a DMA gap.
        // Before the first quiet period only, retry the baseline bootstrap
        // rather than retaining that strike as the baseline indefinitely.
        if (!pad.response.calibrated && !state.initialQuietFound)
        {
            state.startupUnsettled = quietInput ? 0 : state.startupUnsettled + 1;
            if (state.startupUnsettled >= 128)
            {
                state.warmup = 0;
                state.startupUnsettled = 0;
                state.quietFrames = 0;
                continue;
            }
        }
        if (!state.pending)
        {
            if (quietInput)
            {
                state.noise += 0.005f * (peak - state.noise);
                for (std::size_t channel = 0; channel < 2; ++channel)
                {
                    state.sensorNoise[channel] +=
                        0.005f * (amplitudes[channel] - state.sensorNoise[channel]);
                    state.baseline[channel] += 0.01f * (raw[channel] - state.baseline[channel]);
                }
                state.quietFrames = std::min(32U, state.quietFrames + 1);
                if (state.quietFrames >= 16)
                {
                    state.initialQuietFound = true;
                }
            }
            else
            {
                state.quietFrames = 0;
                // Stage one: a new hit is energy above the measured threshold that
                // also exceeds the decaying envelope of the previous hit by the
                // retrigger factor (Mimic "Retrigger Cancel").
                bool onset = false;
                for (unsigned sensor = 0; sensor < sensors && !onset; ++sensor)
                {
                    onset = amplitudes[sensor] >= thresholds[sensor]
                            && amplitudes[sensor] >= state.envelope[sensor] * state.retriggerFactor;
                }
                if (onset && state.initialQuietFound && state.mask == 0)
                {
                    state.onsetBaseline = state.baseline;
                    state.sinceHit = 0;
                    state.pending = true;
                    state.filled = 0;
                    state.peak = 0;
                    state.sensorPeaks = {};
                }
            }
        }
        // The envelope models the previous hit. Shortly after a hit it follows the
        // signal, because the true peak often arrives after the 32-frame window;
        // later it only decays, so a slowly rising onset (a kick beater) can
        // still exceed it.
        const bool following = state.sinceHit < state.followFrames;
        if (state.sinceHit < state.followFrames)
        {
            ++state.sinceHit;
        }
        for (unsigned sensor = 0; sensor < sensors; ++sensor)
        {
            state.envelope[sensor] *= state.decayFactor[sensor];
            if (following)
            {
                state.envelope[sensor] = std::max(state.envelope[sensor], amplitudes[sensor]);
            }
        }
        if (!state.pending)
        {
            continue;
        }
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            const auto value = raw[channel] - state.onsetBaseline[channel];
            state.waveform[channel][state.filled] = value;
            state.peak = std::max(state.peak, std::abs(value));
            state.sensorPeaks[channel] = std::max(state.sensorPeaks[channel], std::abs(value));
        }
        ++state.filled;
        if (state.filled != drumWindowFrames)
        {
            continue;
        }
        state.pending = false;
        state.mask = state.maskFrames > drumWindowFrames
                         ? state.maskFrames - static_cast<unsigned>(drumWindowFrames)
                         : 0U;
        for (unsigned sensor = 0; sensor < sensors; ++sensor)
        {
            state.envelope[sensor] = std::max(state.envelope[sensor], state.sensorPeaks[sensor]);
        }
        if (count == events.size())
        {
            ++rejected;
            continue;
        }
        DrumPrediction prediction;
        bool usedModel = false;
        unsigned ruled = 0;
        const auto zone = decideZone(state, dual, prediction, usedModel, ruled);
        if (usedModel)
        {
            ++modelDecided;
        }
        // Per-articulation response is calibrated separately from classification.
        const auto eventPeak = dual && zone < 3 ? state.sensorPeaks[zone - 1] : state.peak;
        const auto velocity =
            pad.response.calibrated
                ? calibratedVelocity(eventPeak, pad.response.articulations[zone - 1])
                : static_cast<unsigned>(std::clamp(
                      std::lround(127.0 * std::sqrt(static_cast<double>(eventPeak) / 2048.0)),
                      1L,
                      127L));
        events[count++] = {index,
                           zone,
                           velocity,
                           usedModel ? prediction.probabilities[zone] : 1.0f,
                           eventPeak,
                           state.sensorPeaks,
                           ruled,
                           usedModel};
    }
}
} // namespace mimic
