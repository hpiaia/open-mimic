// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <thread>

// The Pearl encoder uses three GPIO value files rather than Linux input events.
// Sampling stays on its own worker; callbacks run on the JUCE message thread.
class WheelInput final : private juce::Timer
{
public:
    WheelInput();
    ~WheelInput() override;

    std::function<void(int)> onRotate;
    std::function<void()> onClick;

private:
    void run();
    void timerCallback() override;

    std::atomic<bool> stopping{false};
    std::atomic<int> pendingSteps{0};
    std::atomic<unsigned> pendingClicks{0};
    std::thread worker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WheelInput)
};
