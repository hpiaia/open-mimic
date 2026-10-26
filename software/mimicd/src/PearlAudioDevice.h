// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace mimic
{
struct PearlAudioLayout
{
    int sampleBytes = 0;
    int channels = 0;
    int framesPerBlock = 0;
    int blockBytes = 0;
};

[[nodiscard]] PearlAudioLayout probePearlAudioLayout();

// The analog device is a vendor McASP transport, independent of the HDMI ALSA device.
// open/start/stop/close belong to one control thread; the callback runs on its worker.
class PearlAudioDevice final : public juce::AudioIODevice, private juce::Thread
{
public:
    PearlAudioDevice();
    ~PearlAudioDevice() override;

    juce::StringArray getOutputChannelNames() override;
    juce::StringArray getInputChannelNames() override;
    juce::Array<double> getAvailableSampleRates() override;
    juce::Array<int> getAvailableBufferSizes() override;
    int getDefaultBufferSize() override;
    juce::String open(const juce::BigInteger& inputChannels,
                      const juce::BigInteger& outputChannels,
                      double sampleRate,
                      int bufferSizeSamples) override;
    void close() override;
    bool isOpen() override;
    void start(juce::AudioIODeviceCallback* callback) override;
    void stop() override;
    bool isPlaying() override;
    juce::String getLastError() override;
    int getCurrentBufferSizeSamples() override;
    double getCurrentSampleRate() override;
    int getCurrentBitDepth() override;
    juce::BigInteger getActiveOutputChannels() const override;
    juce::BigInteger getActiveInputChannels() const override;
    int getOutputLatencyInSamples() override;
    int getInputLatencyInSamples() override;

    [[nodiscard]] PearlAudioLayout getLayout() const noexcept;

private:
    void run() override;
    bool writeBlock() noexcept;
    void flushSilence() noexcept;

    int descriptor = -1;
    int lockDescriptor = -1;
    PearlAudioLayout layout;
    juce::BigInteger activeOutputs;
    juce::AudioBuffer<float> samples;
    std::array<float*, 16> outputPointers{};
    std::vector<std::int32_t> packed;
    juce::AudioIODeviceCallback* callback = nullptr;
    std::atomic<bool> playing{false};
    std::atomic<int> errorCode{0};
    juce::String controlError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PearlAudioDevice)
};
} // namespace mimic
