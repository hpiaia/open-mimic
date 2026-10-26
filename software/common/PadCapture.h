// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace mimic
{
struct PadSnapshot
{
    std::uint64_t elapsedNanoseconds = 0;
    std::array<std::pair<std::uint32_t, std::uint32_t>, 2> cursors{};
    std::array<std::vector<std::uint8_t>, 2> buffers;
};

struct PadCaptureQuality
{
    std::uint64_t invalidCursors = 0;
    std::uint64_t invalidChannelFrames = 0;
    std::uint64_t stalledCursors = 0;
    std::uint64_t pollGaps = 0;
    std::uint64_t copyOverlap = 0;
    std::uint64_t clippedSamples = 0;
    std::uint64_t unexpectedActivity = 0;
    [[nodiscard]] bool usable() const noexcept;
    PadCaptureQuality& operator+=(const PadCaptureQuality& other) noexcept;
};

struct PadSnapshotAnalysis
{
    std::array<std::uint16_t, 32> peaks{};
    PadCaptureQuality quality;
};

// Examines only newly completed frames. Before/after movement is normal; an
// overlap is flagged only if copying may have overwritten that frame interval.
// The 8 ms gap bound is conservative for this board's observed ~11.28 kHz
// cadence and 4096-byte ring. Counters do not establish exact lost-frame counts.
PadSnapshotAnalysis analysePadSnapshot(const PadSnapshot& previous, const PadSnapshot& current);
} // namespace mimic
