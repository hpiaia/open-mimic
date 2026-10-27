// SPDX-License-Identifier: AGPL-3.0-only
#include "TriggerEngine.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace mimic
{
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
        for (std::size_t index = 1; index < pad.gate.size(); ++index)
        {
            if (!std::isfinite(pad.gate[index]) || pad.gate[index] < 0.0f || pad.gate[index] > 1.0f)
            {
                throw std::invalid_argument("Class gate thresholds must be within 0..1");
            }
        }
        PadState state;
        state.input = pad;
        states.push_back(state);
    }
    reset();
}

void TriggerEngine::reset() noexcept
{
    for (auto& state : states)
    {
        const auto input = state.input;
        state = {};
        state.input = input;
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
        const std::array<float, 2> raw{
            static_cast<float>(frame[static_cast<std::size_t>(pad.head % 16)] & 4095U),
            dual ? static_cast<float>(frame[static_cast<std::size_t>(pad.rim % 16)] & 4095U)
                 : 0.0f};
        if (state.cooldown > 0)
        {
            --state.cooldown;
        }
        if (raw[0] >= 4090.0f || (dual && raw[1] >= 4090.0f))
        {
            ++clipped;
            state.pending = false;
            state.armed = false;
            state.quiet = 0;
            state.cooldown = 128;
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
                for (unsigned sensor = 0; sensor < (dual ? 2U : 1U); ++sensor)
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
        bool quietInput = false;
        if (pad.response.calibrated)
        {
            quietInput = true;
            for (unsigned sensor = 0; sensor < (dual ? 2U : 1U); ++sensor)
            {
                const auto& response = pad.response.sensors[sensor];
                const auto threshold =
                    response.threshold
                    * std::max(1.0f, state.sensorNoise[sensor] / response.noiseMean);
                quietInput = quietInput && amplitudes[sensor] < threshold;
            }
        }
        else
        {
            quietInput = peak < std::max(16.0f, 6.0f * state.noise + 8.0f);
        }
        // An uncalibrated warm-up may begin in a strike/decay after a DMA gap.
        // Before the first quiet arming only, retry the baseline bootstrap
        // rather than retaining that strike as the baseline indefinitely.
        if (!pad.response.calibrated && !state.initialQuietFound)
        {
            state.startupUnsettled = quietInput ? 0 : state.startupUnsettled + 1;
            if (state.startupUnsettled >= 128)
            {
                state.warmup = 0;
                state.startupUnsettled = 0;
                state.quiet = 0;
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
                state.quiet = std::min(32U, state.quiet + 1);
                if (state.quiet >= 16 && state.cooldown == 0)
                {
                    state.armed = true;
                    state.initialQuietFound = true;
                }
            }
            else
            {
                state.quiet = 0;
                if (state.armed && state.cooldown == 0)
                {
                    state.onsetBaseline = state.baseline;
                    state.pending = true;
                    state.armed = false;
                    state.filled = 0;
                    state.peak = 0;
                    state.sensorPeaks = {};
                }
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
        state.cooldown = 128;
        state.quiet = 0;
        const auto result = pad.predictor ? pad.predictor(drumFeatures(state.waveform, dual))
                                          : predictor(drumFeatures(state.waveform, dual));
        if (!result.valid || result.classIndex == 0 || result.classIndex > (dual ? 3U : 1U)
            || result.probabilities[result.classIndex] < pad.gate[result.classIndex]
            || count == events.size())
        {
            ++rejected;
            continue;
        }
        // Per-articulation response is calibrated separately from classification.
        const auto eventPeak = pad.response.calibrated && result.classIndex < 3
                                   ? state.sensorPeaks[result.classIndex - 1]
                                   : state.peak;
        const auto velocity =
            pad.response.calibrated
                ? calibratedVelocity(eventPeak, pad.response.articulations[result.classIndex - 1])
                : static_cast<unsigned>(std::clamp(
                      std::lround(127.0 * std::sqrt(static_cast<double>(state.peak) / 2048.0)),
                      1L,
                      127L));
        events[count++] = {index,
                           static_cast<unsigned>(result.classIndex),
                           velocity,
                           result.probabilities[result.classIndex],
                           eventPeak};
    }
}
} // namespace mimic
