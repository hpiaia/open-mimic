// SPDX-License-Identifier: AGPL-3.0-only
#include "AuxControl.h"

#include "AuxInputSettings.h"
#include "PearlAudioBoard.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <thread>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace mimic
{
namespace
{
volatile std::sig_atomic_t stopping = 0;

extern "C" void stopAuxControl(int)
{
    stopping = 1;
}

#if defined(__linux__)
class Descriptor final
{
public:
    explicit Descriptor(int valueToOwn) : value(valueToOwn) {}

    ~Descriptor()
    {
        if (value >= 0)
        {
            ::close(value);
        }
    }

    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;

    int get() const noexcept
    {
        return value;
    }

private:
    int value;
};

void readSettings(AuxInputSettings& settings, float& headphoneGain)
{
    std::ifstream auxFile("/run/mimic-aux-input");
    float aux = 0.0f;
    int follow = 0;
    if (auxFile >> aux >> follow && std::isfinite(aux) && aux >= 0.0f && aux <= 1.0f
        && (follow == 0 || follow == 1))
    {
        settings = {aux, follow != 0};
    }
    std::ifstream headphonesFile("/run/mimic-headphone-volume");
    float headphones = 0.0f;
    if (headphonesFile >> headphones && std::isfinite(headphones) && headphones >= 0.0f
        && headphones <= 1.0f)
    {
        headphoneGain = headphones;
    }
}

void writeAttenuation(int descriptor, std::uint8_t code)
{
    // The original application writes all three addresses in this order.
    for (std::uint8_t channel = 0; channel < 3; ++channel)
    {
        std::array<std::uint8_t, 2> payload{channel, code};
        int result;
        do
        {
            result = ::ioctl(descriptor, 0x5203, payload.data());
        } while (result < 0 && errno == EINTR && stopping == 0);
        if (result < 0)
        {
            throw std::system_error(errno, std::generic_category(), "Setting Aux attenuation");
        }
    }
}
#endif
} // namespace

int runAuxControl()
{
#if defined(__linux__)
    Descriptor lock(::open("/run/mimic-aux-control.lock", O_RDWR | O_CREAT | O_CLOEXEC, 0600));
    if (lock.get() < 0 || ::flock(lock.get(), LOCK_EX | LOCK_NB) != 0)
    {
        throw std::system_error(errno, std::generic_category(), "Acquiring Aux control ownership");
    }
    Descriptor regulator(::open("/dev/regulators", O_RDWR | O_CLOEXEC));
    if (regulator.get() < 0)
    {
        throw std::system_error(errno, std::generic_category(), "Opening Aux attenuator");
    }
    stopping = 0;
    const auto previousInterrupt = std::signal(SIGINT, stopAuxControl);
    const auto previousTerminate = std::signal(SIGTERM, stopAuxControl);
    if (previousInterrupt == SIG_ERR || previousTerminate == SIG_ERR)
    {
        throw std::runtime_error("Could not install Aux control signal handlers");
    }
    AuxInputSettings settings;
    float headphoneGain = 1.0f;
    int previousCode = -1;
    while (stopping == 0)
    {
        readSettings(settings, headphoneGain);
        const auto code = auxAttenuationCode(effectiveAuxGain(settings, headphoneGain));
        if (previousCode != code)
        {
            writeAttenuation(regulator.get(), code);
            if (previousCode < 0)
            {
                enablePearlAnalogOutputs();
            }
            previousCode = code;
            std::cout << "Aux input: " << (settings.followHeadphones ? "linked" : "independent")
                      << ", attenuation code " << previousCode << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // Leave the analog signal at its last requested level during a service restart.
    std::signal(SIGINT, previousInterrupt);
    std::signal(SIGTERM, previousTerminate);
    return 0;
#else
    throw std::runtime_error("Aux input control requires the Pearl Linux regulator device");
#endif
}
} // namespace mimic
