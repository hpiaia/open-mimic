// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_core/juce_core.h>

#include <mutex>

struct NetworkStatus
{
    juce::String ssid = "Checking connection";
    juce::String state = "Checking";
    juce::String address = "Checking address";
};

class NetworkMonitor final : private juce::Thread
{
public:
    NetworkMonitor();
    ~NetworkMonitor() override;
    [[nodiscard]] NetworkStatus snapshot() const;

private:
    static juce::String query(const juce::StringArray& command);
    void run() override;
    mutable std::mutex lock;
    NetworkStatus current;
};
