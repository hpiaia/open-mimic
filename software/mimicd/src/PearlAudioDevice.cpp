// SPDX-License-Identifier: AGPL-3.0-only
#include "PearlAudioDevice.h"

#include "PearlAudioBoard.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <stdexcept>
#include <system_error>

#if JUCE_LINUX
#include <fcntl.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace mimic
{
namespace
{
constexpr double sampleRateHz = 44100.0;
constexpr int channelCount = 16;
constexpr int blockFrames = 64;
constexpr std::int32_t pcmFullScale = 8388607;

#if JUCE_LINUX
class Descriptor final
{
public:
    explicit Descriptor(int descriptorValue) : value(descriptorValue) {}

    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;

    ~Descriptor()
    {
        if (value >= 0)
        {
            ::close(value);
        }
    }

    int release() noexcept
    {
        const auto result = value;
        value = -1;
        return result;
    }

    int get() const noexcept
    {
        return value;
    }

private:
    int value;
};

int acquireAudioLock()
{
    Descriptor descriptor(::open("/run/mimic-audio.lock", O_RDWR | O_CREAT | O_CLOEXEC, 0600));
    if (descriptor.get() < 0 || ::flock(descriptor.get(), LOCK_EX | LOCK_NB) != 0)
    {
        throw std::system_error(errno, std::generic_category(), "Acquiring analog audio ownership");
    }
    return descriptor.release();
}

PearlAudioLayout queryLayout(int descriptor)
{
    const auto query = [descriptor](unsigned long command)
    {
        const auto value = ::ioctl(descriptor, command);
        if (value < 0)
        {
            throw std::system_error(errno, std::generic_category(), "McASP layout ioctl");
        }
        return value;
    };
    const PearlAudioLayout result{query(0x4d01), query(0x4d02), query(0x4d03), query(0x4d04)};
    if (result.sampleBytes != 4 || result.channels != channelCount
        || result.framesPerBlock != blockFrames || result.blockBytes != 4096)
    {
        throw std::runtime_error("Unsupported Pearl McASP layout; expected 4/16/64/4096");
    }
    return result;
}

void initializeAudioClock()
{
    Descriptor regulator(::open("/dev/regulators", O_WRONLY | O_CLOEXEC));
    if (regulator.get() < 0 || ::ioctl(regulator.get(), 0x5201, 0x7021UL) < 0)
    {
        throw std::system_error(errno, std::generic_category(), "Initializing Pearl audio PLL");
    }
}

#endif
} // namespace

PearlAudioLayout probePearlAudioLayout()
{
#if JUCE_LINUX
    Descriptor lock(acquireAudioLock());
    Descriptor descriptor(::open("/dev/mcasp.0", O_RDWR | O_CLOEXEC));
    if (descriptor.get() < 0)
    {
        throw std::system_error(errno, std::generic_category(), "Opening Pearl analog audio");
    }
    return queryLayout(descriptor.get());
#else
    throw std::runtime_error("Pearl analog audio requires the Mimic Linux devices");
#endif
}

PearlAudioDevice::PearlAudioDevice()
    : AudioIODevice("Pearl analog outputs", "Pearl McASP"), Thread("analog-audio")
{
}

PearlAudioDevice::~PearlAudioDevice()
{
    close();
}

juce::StringArray PearlAudioDevice::getOutputChannelNames()
{
    juce::StringArray names;
    for (int channel = 0; channel < channelCount; ++channel)
    {
        names.add("Raw " + juce::String(channel));
    }
    return names;
}

juce::StringArray PearlAudioDevice::getInputChannelNames()
{
    return {};
}

juce::Array<double> PearlAudioDevice::getAvailableSampleRates()
{
    return {sampleRateHz};
}

juce::Array<int> PearlAudioDevice::getAvailableBufferSizes()
{
    return {blockFrames};
}

int PearlAudioDevice::getDefaultBufferSize()
{
    return blockFrames;
}

juce::String PearlAudioDevice::open(const juce::BigInteger& inputChannels,
                                    const juce::BigInteger& outputChannels,
                                    double requestedSampleRate,
                                    int bufferSizeSamples)
{
    close();
    errorCode.store(0, std::memory_order_relaxed);
    controlError.clear();
    if (!inputChannels.isZero() || outputChannels.isZero()
        || outputChannels.getHighestBit() >= channelCount || !std::isfinite(requestedSampleRate)
        || std::abs(requestedSampleRate - sampleRateHz) > 0.01 || bufferSizeSamples != blockFrames)
    {
        controlError = "Pearl analog output requires 44100 Hz, 64 frames, and output channels 0–15";
        return controlError;
    }
#if JUCE_LINUX
    try
    {
        Descriptor lock(acquireAudioLock());
        initializeAudioClock();
        Descriptor device(::open("/dev/mcasp.0", O_RDWR | O_CLOEXEC));
        if (device.get() < 0)
        {
            throw std::system_error(errno, std::generic_category(), "Opening Pearl analog audio");
        }
        layout = queryLayout(device.get());
        activeOutputs = outputChannels;
        samples.setSize(channelCount, blockFrames);
        samples.clear();
        packed.assign(static_cast<std::size_t>(channelCount * blockFrames), 0);
        enablePearlAnalogOutputs();
        for (int channel = 0; channel < channelCount; ++channel)
        {
            outputPointers[static_cast<std::size_t>(channel)] =
                activeOutputs[channel] ? samples.getWritePointer(channel) : nullptr;
        }
        descriptor = device.release();
        lockDescriptor = lock.release();
    }
    catch (const std::exception& error)
    {
        controlError = error.what();
    }
#else
    controlError = "Pearl analog audio requires the Mimic Linux devices";
#endif
    return controlError;
}

void PearlAudioDevice::close()
{
    stop();
#if JUCE_LINUX
    if (descriptor >= 0)
    {
        ::close(descriptor);
        descriptor = -1;
    }
    if (lockDescriptor >= 0)
    {
        ::close(lockDescriptor);
        lockDescriptor = -1;
    }
#endif
    activeOutputs.clear();
}

bool PearlAudioDevice::isOpen()
{
    return descriptor >= 0;
}

void PearlAudioDevice::start(juce::AudioIODeviceCallback* nextCallback)
{
    stop();
    if (!isOpen() || nextCallback == nullptr)
    {
        controlError = "Open the analog device before starting its callback";
        return;
    }
    callback = nextCallback;
    callback->audioDeviceAboutToStart(this);
    playing.store(true, std::memory_order_release);
    const auto options =
        juce::Thread::RealtimeOptions{}.withPriority(5).withApproximateAudioProcessingTime(
            blockFrames, sampleRateHz);
    if (!startRealtimeThread(options) && !startThread(juce::Thread::Priority::high))
    {
        playing.store(false, std::memory_order_release);
        controlError = "Could not start the analog audio worker";
        callback->audioDeviceStopped();
        callback = nullptr;
    }
}

void PearlAudioDevice::stop()
{
    signalThreadShouldExit();
    stopThread(-1);
    playing.store(false, std::memory_order_release);
    if (callback != nullptr)
    {
        callback->audioDeviceStopped();
        callback = nullptr;
    }
}

bool PearlAudioDevice::isPlaying()
{
    return playing.load(std::memory_order_acquire);
}

juce::String PearlAudioDevice::getLastError()
{
    const auto code = errorCode.load(std::memory_order_acquire);
    return code != 0 ? juce::String(std::generic_category().message(code)) : controlError;
}

int PearlAudioDevice::getCurrentBufferSizeSamples()
{
    return blockFrames;
}

double PearlAudioDevice::getCurrentSampleRate()
{
    return sampleRateHz;
}

int PearlAudioDevice::getCurrentBitDepth()
{
    return 24;
}

juce::BigInteger PearlAudioDevice::getActiveOutputChannels() const
{
    return activeOutputs;
}

juce::BigInteger PearlAudioDevice::getActiveInputChannels() const
{
    return {};
}

int PearlAudioDevice::getOutputLatencyInSamples()
{
    return blockFrames * 2;
}

int PearlAudioDevice::getInputLatencyInSamples()
{
    return 0;
}

PearlAudioLayout PearlAudioDevice::getLayout() const noexcept
{
    return layout;
}

bool PearlAudioDevice::writeBlock() noexcept
{
#if JUCE_LINUX
    ssize_t written = -1;
    do
    {
        written = ::write(descriptor, packed.data(), static_cast<std::size_t>(layout.blockBytes));
    } while (written < 0 && errno == EINTR);
    if (written != layout.blockBytes)
    {
        errorCode.store(written < 0 ? errno : EIO, std::memory_order_release);
        return false;
    }
    return true;
#else
    return false;
#endif
}

void PearlAudioDevice::flushSilence() noexcept
{
    std::fill(packed.begin(), packed.end(), 0);
    for (int portion = 0; portion < 2; ++portion)
    {
        if (!writeBlock())
        {
            break;
        }
    }
}

void PearlAudioDevice::run()
{
    try
    {
        while (!threadShouldExit())
        {
            samples.clear();
            callback->audioDeviceIOCallbackWithContext(
                nullptr, 0, outputPointers.data(), channelCount, blockFrames, {});
            std::size_t index = 0;
            for (int frame = 0; frame < blockFrames; ++frame)
            {
                for (int channel = 0; channel < channelCount; ++channel)
                {
                    const auto sample = samples.getSample(channel, frame);
                    const auto bounded =
                        std::isfinite(sample) ? juce::jlimit(-1.0f, 1.0f, sample) : 0.0f;
                    packed[index++] = static_cast<std::int32_t>(bounded * pcmFullScale);
                }
            }
            if (!writeBlock())
            {
                break;
            }
        }
    }
    catch (...)
    {
        errorCode.store(EIO, std::memory_order_release);
    }
    flushSilence();
    playing.store(false, std::memory_order_release);
}
} // namespace mimic
