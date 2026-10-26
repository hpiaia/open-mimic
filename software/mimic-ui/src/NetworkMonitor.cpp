// SPDX-License-Identifier: AGPL-3.0-only
#include "NetworkMonitor.h"

NetworkMonitor::NetworkMonitor() : Thread("network-status")
{
    startThread();
}

NetworkMonitor::~NetworkMonitor()
{
    signalThreadShouldExit();
    notify();
    stopThread(-1);
}

NetworkStatus NetworkMonitor::snapshot() const
{
    const std::lock_guard<std::mutex> guard(lock);
    return current;
}

juce::String NetworkMonitor::query(const juce::StringArray& command)
{
    juce::ChildProcess process;
    if (!process.start(command))
    {
        return {};
    }
    if (!process.waitForProcessToFinish(2000))
    {
        process.kill();
        return {};
    }
    if (process.getExitCode() != 0)
    {
        return {};
    }
    return process.readAllProcessOutput();
}

void NetworkMonitor::run()
{
    while (!threadShouldExit())
    {
        NetworkStatus next;
#if JUCE_LINUX
        const auto status =
            query({"wpa_cli", "-p", "/tmp/wpa_supplicant", "-i", "wlan0", "status"});
        next.ssid = "Disconnected";
        next.state = "Unavailable";
        next.address = "No IPv4 address";
        for (const auto& line : juce::StringArray::fromLines(status))
        {
            if (line.startsWith("ssid="))
            {
                next.ssid = line.substring(5);
            }
            if (line.startsWith("wpa_state="))
            {
                next.state = line.substring(10) == "COMPLETED" ? "Connected" : line.substring(10);
            }
            if (line.startsWith("ip_address="))
            {
                next.address = line.substring(11);
            }
        }
        if (next.address == "No IPv4 address")
        {
            const auto ip = query({"ip", "-4", "-o", "addr", "show", "dev", "wlan0"});
            const auto tokens = juce::StringArray::fromTokens(ip, false);
            const auto index = tokens.indexOf("inet");
            if (index >= 0 && index + 1 < tokens.size())
            {
                next.address = tokens[index + 1];
            }
        }
#else
        next = {"Desktop preview", "Preview", "Not running on Mimic"};
#endif
        {
            const std::lock_guard<std::mutex> guard(lock);
            current = std::move(next);
        }
        wait(3000);
    }
}
