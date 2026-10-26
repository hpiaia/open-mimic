// SPDX-License-Identifier: AGPL-3.0-only
#include "AudioEngine.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace mimic
{
static_assert(std::atomic<float>::is_always_lock_free,
              "Audio parameters require lock-free atomics");

void AudioEngine::setMasterGainLinear(float value) noexcept
{
    if (std::isfinite(value))
    {
        requestedGain.store(juce::jlimit(0.0f, 2.0f, value), std::memory_order_relaxed);
    }
}

void AudioEngine::prepare(double sampleRate, std::uint32_t maximumBlockSize, std::uint32_t channels)
{
    constexpr auto maximumBufferDimension =
        static_cast<std::uint32_t>(std::numeric_limits<int>::max());
    if (!std::isfinite(sampleRate) || sampleRate <= 0 || maximumBlockSize == 0 || channels == 0
        || maximumBlockSize > maximumBufferDimension || channels > maximumBufferDimension)
    {
        throw std::invalid_argument("Invalid audio preparation settings");
    }
    maximumFrames = maximumBlockSize;
    preparedChannels = channels;
    masterGain.setRampDurationSeconds(0.01);
    masterGain.setGainLinear(requestedGain.load(std::memory_order_relaxed));
    masterGain.prepare({sampleRate, maximumBlockSize, channels});
}

bool AudioEngine::process(juce::AudioBuffer<float>& buffer) noexcept
{
    if (maximumFrames == 0 || buffer.getNumSamples() > static_cast<int>(maximumFrames)
        || buffer.getNumChannels() != static_cast<int>(preparedChannels))
    {
        buffer.clear();
        return false;
    }
    masterGain.setGainLinear(requestedGain.load(std::memory_order_relaxed));
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    masterGain.process(context);
    return true;
}

int audioSelfTest()
{
    AudioEngine engine;
    engine.setMasterGainLinear(0.25f);
    engine.prepare(48000.0, 128, 2);
    juce::AudioBuffer<float> buffer(2, 128);
    for (int channel = 0; channel < 2; ++channel)
    {
        for (int sample = 0; sample < 128; ++sample)
        {
            buffer.setSample(channel, sample, 0.5f);
        }
    }
    if (!engine.process(buffer) || std::abs(buffer.getSample(0, 0) - 0.125f) > 1.0e-6f)
    {
        throw std::runtime_error("JUCE DSP gain validation failed");
    }
    juce::AudioBuffer<float> oversized(2, 256);
    if (engine.process(oversized) || std::fpclassify(oversized.getMagnitude(0, 256)) != FP_ZERO)
    {
        throw std::runtime_error("Audio engine accepted an oversized block");
    }
    bool invalidDimensionsRejected = false;
    try
    {
        engine.prepare(48000.0, std::numeric_limits<std::uint32_t>::max(), 2);
    }
    catch (const std::invalid_argument&)
    {
        invalidDimensionsRejected = true;
    }
    if (!invalidDimensionsRejected)
    {
        throw std::runtime_error("Audio engine accepted dimensions outside JUCE's int range");
    }
    std::cout << "JUCE DSP offline check passed: 2 channels, 128 frames, 48000 Hz\n"
                 "Pearl audio output is not connected; no sound was played.\n";
    return 0;
}
} // namespace mimic
