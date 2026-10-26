// SPDX-License-Identifier: AGPL-3.0-only
#include "SystemMonitor.h"

#include <array>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
struct CpuTimes
{
    std::uint64_t total = 0;
    std::uint64_t idle = 0;
};

std::optional<CpuTimes> readCpuTimes()
{
    std::ifstream input("/proc/stat");
    std::string name;
    std::array<std::uint64_t, 8> times{};
    if (!(input >> name) || name != "cpu")
    {
        return std::nullopt;
    }

    CpuTimes result;
    // guest and guest_nice are already included in user/nice; count the first eight fields.
    for (auto& time : times)
    {
        if (!(input >> time))
        {
            return std::nullopt;
        }
        result.total += time;
    }
    result.idle = times[3] + times[4];
    return result;
}

std::optional<MemoryUsage> readMemoryUsage()
{
    std::ifstream input("/proc/meminfo");
    std::optional<std::uint64_t> total;
    std::optional<std::uint64_t> available;
    std::string line;
    while (std::getline(input, line))
    {
        std::istringstream fields(line);
        std::string key;
        std::string unit;
        std::uint64_t value = 0;
        if (fields >> key >> value >> unit && unit == "kB")
        {
            if (key == "MemTotal:")
            {
                total = value;
            }
            else if (key == "MemAvailable:")
            {
                available = value;
            }
        }
        if (total && available)
        {
            break;
        }
    }
    if (!total || !available || *total == 0 || *available > *total)
    {
        return std::nullopt;
    }

    // Available memory includes reclaimable cache, avoiding inflated RAM usage.
    return MemoryUsage{*total - *available, *total};
}
} // namespace

SystemMonitor::SystemMonitor() : Thread("system-status")
{
    startThread();
}

SystemMonitor::~SystemMonitor()
{
    signalThreadShouldExit();
    notify();
    stopThread(-1);
}

SystemStatus SystemMonitor::snapshot() const
{
    const std::lock_guard<std::mutex> guard(lock);
    return current;
}

void SystemMonitor::run()
{
    std::optional<CpuTimes> previous;
    while (!threadShouldExit())
    {
        SystemStatus next;
        const auto cpu = readCpuTimes();
        if (cpu && previous && cpu->total > previous->total && cpu->idle >= previous->idle)
        {
            const auto totalDelta = cpu->total - previous->total;
            const auto idleDelta = cpu->idle - previous->idle;
            if (idleDelta <= totalDelta)
            {
                next.cpuPercent = 100.0 * static_cast<double>(totalDelta - idleDelta)
                                  / static_cast<double>(totalDelta);
            }
        }
        previous = cpu;
        next.memory = readMemoryUsage();
        {
            const std::lock_guard<std::mutex> guard(lock);
            current = next;
        }
        wait(1000);
    }
}
