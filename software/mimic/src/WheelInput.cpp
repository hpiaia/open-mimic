// SPDX-License-Identifier: AGPL-3.0-only
#include "WheelInput.h"

#include <algorithm>
#include <chrono>
#include <string>

#if JUCE_LINUX
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
#if JUCE_LINUX
class GpioInput final
{
public:
    explicit GpioInput(int number)
    {
        const auto path = "/sys/class/gpio/gpio" + std::to_string(number);
        if (::access(path.c_str(), F_OK) != 0)
        {
            const auto exporter = ::open("/sys/class/gpio/export", O_WRONLY | O_CLOEXEC);
            if (exporter < 0)
            {
                return;
            }
            const auto text = std::to_string(number);
            const auto written = ::write(exporter, text.data(), text.size());
            const auto error = errno;
            ::close(exporter);
            if (written != static_cast<ssize_t>(text.size()) && error != EBUSY)
            {
                return;
            }
        }
        const auto direction = ::open((path + "/direction").c_str(), O_WRONLY | O_CLOEXEC);
        if (direction < 0)
        {
            return;
        }
        const auto written = ::write(direction, "in", 2);
        ::close(direction);
        if (written == 2)
        {
            descriptor = ::open((path + "/value").c_str(), O_RDONLY | O_CLOEXEC);
        }
    }

    ~GpioInput()
    {
        if (descriptor >= 0)
        {
            ::close(descriptor);
        }
    }

    int read() const noexcept
    {
        char value = 0;
        return ::pread(descriptor, &value, 1, 0) == 1 && (value == '0' || value == '1')
                   ? value - '0'
                   : -1;
    }

    GpioInput(const GpioInput&) = delete;
    GpioInput& operator=(const GpioInput&) = delete;

private:
    int descriptor = -1;
};
#endif
} // namespace

WheelInput::WheelInput() : worker([this] { run(); })
{
    startTimerHz(50);
}

WheelInput::~WheelInput()
{
    stopTimer();
    stopping.store(true, std::memory_order_release);
    worker.join();
}

void WheelInput::run()
{
#if JUCE_LINUX
    GpioInput button(190);
    GpioInput phaseA(191);
    GpioInput phaseB(192);
    auto previousA = phaseA.read();
    auto lastButton = button.read();
    auto stableButton = lastButton;
    if (previousA < 0 || lastButton < 0 || phaseB.read() < 0)
    {
        juce::Logger::outputDebugString("Could not open Pearl wheel GPIO inputs 190–192");
        return;
    }
    using Clock = std::chrono::steady_clock;
    auto buttonChanged = Clock::now();
    auto nextSample = Clock::now();
    while (!stopping.load(std::memory_order_acquire))
    {
        const auto now = Clock::now();
        const auto a = phaseA.read();
        const auto b = phaseB.read();
        const auto currentButton = button.read();
        if (a >= 0 && b >= 0)
        {
            // Match the original CEncoderData: falling A, B high means +1.
            if (previousA == 1 && a == 0)
            {
                pendingSteps.fetch_add(b == 1 ? 1 : -1, std::memory_order_relaxed);
            }
            previousA = a;
        }
        if (currentButton >= 0)
        {
            if (currentButton != lastButton)
            {
                lastButton = currentButton;
                buttonChanged = now;
            }
            if (currentButton != stableButton
                && now - buttonChanged >= std::chrono::milliseconds(25))
            {
                stableButton = currentButton;
                if (stableButton == 0)
                {
                    pendingClicks.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        // Avoid a catch-up spin if scheduling temporarily delays the worker.
        nextSample = std::max(nextSample + std::chrono::milliseconds(1), now);
        std::this_thread::sleep_until(nextSample);
    }
#endif
}

void WheelInput::timerCallback()
{
    const auto steps = pendingSteps.exchange(0, std::memory_order_relaxed);
    if (steps != 0 && onRotate)
    {
        onRotate(steps);
    }
    const auto clicks = pendingClicks.exchange(0, std::memory_order_relaxed);
    if (onClick)
    {
        for (unsigned click = 0; click < clicks; ++click)
        {
            onClick();
        }
    }
}
