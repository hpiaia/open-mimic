// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <mutex>
#include <optional>

struct MemoryUsage
{
    std::uint64_t usedKiB = 0;
    std::uint64_t totalKiB = 0;
};

struct SystemStatus
{
    std::optional<double> cpuPercent;
    std::optional<MemoryUsage> memory;
};

class SystemMonitor final : private juce::Thread
{
public:
    SystemMonitor();
    ~SystemMonitor() override;
    [[nodiscard]] SystemStatus snapshot() const;

private:
    void run() override;

    mutable std::mutex lock;
    SystemStatus current;
};
