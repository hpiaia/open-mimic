// SPDX-License-Identifier: AGPL-3.0-only
#include "PadCapture.h"

#include "AdcFrame.h"

#include <algorithm>

namespace mimic
{
bool PadCaptureQuality::usable() const noexcept
{
    return invalidCursors == 0 && invalidChannelFrames == 0 && stalledCursors == 0 && pollGaps == 0
           && copyOverlap == 0 && clippedSamples == 0 && unexpectedActivity == 0;
}

PadCaptureQuality& PadCaptureQuality::operator+=(const PadCaptureQuality& other) noexcept
{
    invalidCursors += other.invalidCursors;
    invalidChannelFrames += other.invalidChannelFrames;
    stalledCursors += other.stalledCursors;
    pollGaps += other.pollGaps;
    copyOverlap += other.copyOverlap;
    clippedSamples += other.clippedSamples;
    unexpectedActivity += other.unexpectedActivity;
    return *this;
}

PadSnapshotAnalysis analysePadSnapshot(const PadSnapshot& previous, const PadSnapshot& current)
{
    PadSnapshotAnalysis result;
    if (current.elapsedNanoseconds <= previous.elapsedNanoseconds
        || current.elapsedNanoseconds - previous.elapsedNanoseconds > 8000000)
    {
        ++result.quality.pollGaps;
    }
    for (std::size_t adc = 0; adc < 2; ++adc)
    {
        const auto size = current.buffers[adc].size();
        const auto before = current.cursors[adc].first;
        const auto after = current.cursors[adc].second;
        const auto prior = previous.cursors[adc].first;
        const auto valid = [size](std::uint32_t cursor)
        { return cursor > 0 && cursor <= size && cursor % 32 == 0; };
        if (size != 4096 || !valid(prior) || !valid(before) || !valid(after))
        {
            ++result.quality.invalidCursors;
            continue;
        }
        const auto distance = [size](std::uint32_t first, std::uint32_t last)
        { return (static_cast<std::size_t>(last) + size - first) % size; };
        const auto completed = distance(prior, before);
        if (completed == 0)
        {
            ++result.quality.stalledCursors;
        }
        const auto copiedWhileAdvancing = distance(before, after);
        if (completed + copiedWhileAdvancing >= size)
        {
            ++result.quality.copyOverlap;
        }
        auto offset = static_cast<std::size_t>(prior) % size;
        for (std::size_t consumed = 0; consumed < completed; consumed += 32)
        {
            std::array<std::uint16_t, 16> raw{}, channels{};
            for (std::size_t slot = 0; slot < 16; ++slot)
            {
                const auto byte = offset + slot * 2;
                raw[slot] = static_cast<std::uint16_t>(
                    current.buffers[adc][byte]
                    | (static_cast<unsigned>(current.buffers[adc][byte + 1]) << 8));
            }
            if (!decodeAdcFrame(raw, channels))
            {
                ++result.quality.invalidChannelFrames;
            }
            else
            {
                for (std::size_t channel = 0; channel < 16; ++channel)
                {
                    const auto sample = channels[channel];
                    auto& peak = result.peaks[adc * 16 + channel];
                    peak = std::max(peak, sample);
                    if (sample >= 4090)
                    {
                        ++result.quality.clippedSamples;
                    }
                }
            }
            offset = (offset + 32) % size;
        }
    }
    return result;
}
} // namespace mimic
