// SPDX-License-Identifier: AGPL-3.0-only
#include "AudioTest.h"

#include "AudioEngine.h"
#include "PearlAudioDevice.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <system_error>

#if JUCE_LINUX
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#include <csignal>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace mimic
{
namespace
{
volatile std::sig_atomic_t interrupted = 0;

extern "C" void interruptAudioTest(int)
{
    interrupted = 1;
}

class SignalHandlers final
{
public:
    SignalHandlers()
    {
        interrupted = 0;
        previousInterrupt = std::signal(SIGINT, interruptAudioTest);
        previousTerminate = std::signal(SIGTERM, interruptAudioTest);
        if (previousInterrupt == SIG_ERR || previousTerminate == SIG_ERR)
        {
            if (previousInterrupt != SIG_ERR)
            {
                std::signal(SIGINT, previousInterrupt);
            }
            if (previousTerminate != SIG_ERR)
            {
                std::signal(SIGTERM, previousTerminate);
            }
            throw std::runtime_error("Could not install audio test cancellation handlers");
        }
    }

    SignalHandlers(const SignalHandlers&) = delete;
    SignalHandlers& operator=(const SignalHandlers&) = delete;

    ~SignalHandlers()
    {
        std::signal(SIGINT, previousInterrupt);
        std::signal(SIGTERM, previousTerminate);
    }

private:
    using Handler = void (*)(int);
    Handler previousInterrupt = SIG_DFL;
    Handler previousTerminate = SIG_DFL;
};

#if JUCE_LINUX
constexpr const char* controlPath = "/run/mimic-audio-control.sock";

sockaddr_un controlAddress()
{
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strcpy(address.sun_path, controlPath);
    return address;
}

class TestControl final
{
public:
    TestControl()
    {
        descriptor = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (descriptor < 0)
        {
            throw std::system_error(errno, std::generic_category(), "Opening audio test control");
        }
        // The device lock is held, so any previous endpoint is stale.
        ::unlink(controlPath);
        const auto address = controlAddress();
        if (::bind(descriptor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0
            || ::chmod(controlPath, 0600) != 0)
        {
            const auto error = errno;
            ::close(descriptor);
            ::unlink(controlPath);
            throw std::system_error(error, std::generic_category(), "Binding audio test control");
        }
    }

    TestControl(const TestControl&) = delete;
    TestControl& operator=(const TestControl&) = delete;

    ~TestControl()
    {
        ::close(descriptor);
        ::unlink(controlPath);
    }

    bool stopRequested()
    {
        char command[16]{};
        const auto received = ::recv(descriptor, command, sizeof(command), 0);
        if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
        {
            throw std::system_error(errno, std::generic_category(), "Reading audio test control");
        }
        return received == 4 && std::memcmp(command, "stop", 4) == 0;
    }

private:
    int descriptor = -1;
};
#else
class TestControl final
{
public:
    bool stopRequested() const noexcept
    {
        return false;
    }
};
#endif

class SineTest final : public juce::AudioIODeviceCallback
{
public:
    explicit SineTest(AudioTestOptions testOptions) : options(testOptions)
    {
        oscillator.initialise([](float phase) { return std::sin(phase); });
    }

    void setHeadphoneGain(float gain) noexcept
    {
        headphoneGain.store(gain, std::memory_order_relaxed);
    }

    bool finished() const noexcept
    {
        return complete.load(std::memory_order_acquire);
    }

    bool failed() const noexcept
    {
        return failure.load(std::memory_order_acquire);
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        const auto rate = device->getCurrentSampleRate();
        const auto frames = device->getCurrentBufferSizeSamples();
        samples.setSize(16, frames);
        samples.clear();
        engine.prepare(rate, static_cast<std::uint32_t>(frames), 16);
        oscillator.prepare({rate, static_cast<std::uint32_t>(frames), 1});
        oscillator.setFrequency(static_cast<float>(options.frequencyHz), true);
        totalFrames = static_cast<std::uint64_t>(std::llround(options.durationSeconds * rate));
        fadeFrames = static_cast<std::uint64_t>(std::llround(0.02 * rate));
        volume.reset(rate, 0.02);
        volume.setCurrentAndTargetValue(headphoneGain.load(std::memory_order_relaxed));
        generatedFrames = 0;
        complete.store(false, std::memory_order_relaxed);
        failure.store(false, std::memory_order_relaxed);
    }

    void audioDeviceIOCallbackWithContext(const float* const*,
                                          int,
                                          float* const* outputs,
                                          int outputChannels,
                                          int frames,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        for (int channel = 0; channel < outputChannels; ++channel)
        {
            if (outputs[channel] != nullptr)
            {
                std::fill_n(outputs[channel], frames, 0.0f);
            }
        }
        if (frames > samples.getNumSamples() || outputChannels > 16)
        {
            failure.store(true, std::memory_order_release);
            return;
        }

        samples.clear();
        volume.setTargetValue(headphoneGain.load(std::memory_order_relaxed));
        for (int frame = 0; frame < frames && generatedFrames < totalFrames; ++frame)
        {
            const auto currentVolume = volume.getNextValue();
            const auto attack = std::min(
                1.0, static_cast<double>(generatedFrames) / static_cast<double>(fadeFrames));
            const auto release = std::min(1.0,
                                          static_cast<double>(totalFrames - generatedFrames - 1)
                                              / static_cast<double>(fadeFrames));
            const auto value = oscillator.processSample(0.0f) * options.amplitude
                               * static_cast<float>(std::min(attack, release));
            for (int channel = 0; channel < outputChannels; ++channel)
            {
                if (outputs[channel] != nullptr && (options.channelMask & (1U << channel)) != 0)
                {
                    samples.setSample(channel,
                                      frame,
                                      value * (isHeadphoneChannel(channel) ? currentVolume : 1.0f));
                }
            }
            ++generatedFrames;
        }
        if (!engine.process(samples))
        {
            failure.store(true, std::memory_order_release);
            return;
        }
        for (int channel = 0; channel < outputChannels; ++channel)
        {
            if (outputs[channel] != nullptr)
            {
                std::copy_n(samples.getReadPointer(channel), frames, outputs[channel]);
            }
        }
        if (generatedFrames >= totalFrames)
        {
            complete.store(true, std::memory_order_release);
        }
    }

    void audioDeviceStopped() override {}

    void audioDeviceError(const juce::String&) override
    {
        failure.store(true, std::memory_order_release);
    }

private:
    std::atomic<float> headphoneGain{1.0f};
    juce::SmoothedValue<float> volume;
    AudioTestOptions options;
    AudioEngine engine;
    juce::dsp::Oscillator<float> oscillator;
    juce::AudioBuffer<float> samples;
    std::uint64_t totalFrames = 0;
    std::uint64_t generatedFrames = 0;
    std::uint64_t fadeFrames = 1;
    std::atomic<bool> complete{false};
    std::atomic<bool> failure{false};
};
} // namespace

int audioToneSelfTest()
{
    AudioTestOptions options;
    options.amplitude = 0.01f;
    SineTest generator(options);
    PearlAudioDevice device;
    generator.setHeadphoneGain(1.0f);
    generator.audioDeviceAboutToStart(&device);
    juce::AudioBuffer<float> outputs(16, 64);
    float peak = 0.0f;
    for (int block = 0; block < 100; ++block)
    {
        generator.audioDeviceIOCallbackWithContext(
            nullptr, 0, outputs.getArrayOfWritePointers(), 16, 64, {});
        peak = std::max(peak, outputs.getMagnitude(headphoneLeftChannel, 0, 64));
        for (int channel = 0; channel < 16; ++channel)
        {
            if (!isHeadphoneChannel(channel) && outputs.getMagnitude(channel, 0, 64) != 0.0f)
            {
                throw std::runtime_error("Tone leaked into an unselected output");
            }
        }
    }
    if (generator.failed() || peak < 0.009f || peak > 0.01001f)
    {
        throw std::runtime_error("Tone generation did not produce its expected amplitude");
    }
    generator.setHeadphoneGain(0.0f);
    for (int block = 0; block < 32; ++block)
    {
        generator.audioDeviceIOCallbackWithContext(
            nullptr, 0, outputs.getArrayOfWritePointers(), 16, 64, {});
    }
    if (outputs.getMagnitude(headphoneLeftChannel, 0, 64) != 0.0f
        || outputs.getMagnitude(headphoneRightChannel, 0, 64) != 0.0f)
    {
        throw std::runtime_error("Headphone mute did not silence the selected outputs");
    }
    // Muting the headphone bus must leave the master bus untouched.
    options.channelMask = (1U << headphoneLeftChannel) | (1U << masterLeftChannel);
    SineTest mixedGenerator(options);
    mixedGenerator.setHeadphoneGain(0.0f);
    mixedGenerator.audioDeviceAboutToStart(&device);
    for (int block = 0; block < 32; ++block)
    {
        mixedGenerator.audioDeviceIOCallbackWithContext(
            nullptr, 0, outputs.getArrayOfWritePointers(), 16, 64, {});
    }
    if (outputs.getMagnitude(headphoneLeftChannel, 0, 64) != 0.0f
        || outputs.getMagnitude(masterLeftChannel, 0, 64) < 0.009f)
    {
        throw std::runtime_error("Headphone mute affected the master output");
    }
    std::cout << "Offline sine routing and headphone mute checks passed; peak " << peak
              << "; no hardware opened.\n";
    return 0;
}

float readHeadphoneGain()
{
    float gain = 1.0f;
    std::ifstream input("/run/mimic-headphone-volume");
    float stored = 0.0f;
    if (input >> stored && std::isfinite(stored) && stored >= 0.0f && stored <= 1.0f)
    {
        gain = stored;
    }
    return gain;
}

int audioProbe()
{
    const auto layout = probePearlAudioLayout();
    std::cout << "Pearl analog audio: 44100 Hz, " << layout.channels << " channels, "
              << layout.framesPerBlock << " frames/block, " << layout.sampleBytes
              << " bytes/sample, " << layout.blockBytes << " bytes/write\n"
              << "Physical sample format: signed 24-bit PCM in 32-bit slots.\n";
    return 0;
}

int runAudioTest(const AudioTestOptions& options)
{
    if (options.channelMask == 0 || !std::isfinite(options.frequencyHz)
        || options.frequencyHz < 20.0 || options.frequencyHz > 10000.0
        || !std::isfinite(options.durationSeconds) || options.durationSeconds < 0.1
        || options.durationSeconds > 60.0 || !std::isfinite(options.amplitude)
        || options.amplitude <= 0.0f || options.amplitude > 1.0f)
    {
        throw std::invalid_argument(
            "Audio test requires channels 0–15, 20–10000 Hz, 0.1–60 seconds, and amplitude 0–1");
    }

    SignalHandlers signals;
    SineTest generator(options);
    // Device must be destroyed before the callback it borrows.
    PearlAudioDevice device;
    juce::BigInteger channels;
    for (int channel = 0; channel < 16; ++channel)
    {
        channels.setBit(channel, (options.channelMask & (1U << channel)) != 0);
    }
    const auto error = device.open({}, channels, 44100.0, 64);
    if (error.isNotEmpty())
    {
        throw std::runtime_error(error.toStdString());
    }

    TestControl control;
    bool stopped = false;
    std::cout << "Playing " << options.frequencyHz << " Hz for " << options.durationSeconds
              << " seconds; peak amplitude " << options.amplitude << "; raw channels:";
    for (int channel = 0; channel < 16; ++channel)
    {
        if (channels[channel])
        {
            std::cout << ' ' << channel;
        }
    }
    std::cout << std::endl;
    generator.setHeadphoneGain(readHeadphoneGain());
    device.start(&generator);
    unsigned controlPoll = 0;
    while (device.isPlaying() && !generator.finished() && !generator.failed() && interrupted == 0)
    {
        if (control.stopRequested())
        {
            stopped = true;
            break;
        }
        if (++controlPoll % 10 == 0)
        {
            generator.setHeadphoneGain(readHeadphoneGain());
        }
        juce::Thread::sleep(5);
    }
    device.stop();
    const auto deviceError = device.getLastError();
    if (deviceError.isNotEmpty())
    {
        throw std::runtime_error(deviceError.toStdString());
    }
    if (generator.failed())
    {
        throw std::runtime_error("The audio callback rejected its processing block");
    }
    std::cout << (interrupted != 0 || stopped ? "Audio test cancelled; outputs silenced.\n"
                                              : "Audio test complete; outputs silenced.\n");
    return interrupted != 0 ? 130 : 0;
}

int stopAudioTest()
{
#if JUCE_LINUX
    const auto descriptor = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (descriptor < 0)
    {
        throw std::system_error(errno, std::generic_category(), "Opening audio stop control");
    }
    const auto address = controlAddress();
    const auto sent = ::sendto(descriptor,
                               "stop",
                               4,
                               MSG_DONTWAIT,
                               reinterpret_cast<const sockaddr*>(&address),
                               sizeof(address));
    const auto error = errno;
    ::close(descriptor);
    if (sent != 4)
    {
        if (error == ENOENT || error == ECONNREFUSED)
        {
            std::cout << "No audio test is running.\n";
            return 0;
        }
        throw std::system_error(error, std::generic_category(), "Requesting audio test stop");
    }
    std::cout << "Audio test stop requested.\n";
    return 0;
#else
    throw std::runtime_error("Audio test control requires Linux");
#endif
}
} // namespace mimic
