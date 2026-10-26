// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <cstdint>

namespace mimic
{
// Called by the future audio-device adapter. Preparation happens off the audio thread.
class AudioEngine final
{
public:
    void prepare(double sampleRate, std::uint32_t maximumBlockSize, std::uint32_t channels);
    [[nodiscard]] bool process(juce::AudioBuffer<float>& buffer) noexcept;
    void setMasterGainLinear(float value) noexcept;

private:
    juce::dsp::Gain<float> masterGain;
    std::atomic<float> requestedGain{1.0f};
    std::uint32_t maximumFrames = 0;
    std::uint32_t preparedChannels = 0;
};

int audioSelfTest();
} // namespace mimic
