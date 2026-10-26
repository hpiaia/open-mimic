// SPDX-License-Identifier: AGPL-3.0-only
#include "AdcFrame.h"
#include "PadCapture.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

mimic::PadSnapshot snapshot(std::uint64_t time, std::uint32_t before, std::uint32_t after)
{
    mimic::PadSnapshot value;
    value.elapsedNanoseconds = time;
    value.cursors = {{{before, after}, {before, after}}};
    value.buffers = {std::vector<std::uint8_t>(4096), std::vector<std::uint8_t>(4096)};
    for (auto& buffer : value.buffers)
    {
        for (std::size_t offset = 0; offset < buffer.size(); offset += 32)
        {
            for (std::size_t channel = 0; channel < 16; ++channel)
            {
                buffer[offset + channel * 2 + 1] = static_cast<std::uint8_t>(channel << 4);
            }
        }
    }
    return value;
}

void sample(mimic::PadSnapshot& value,
            std::size_t adc,
            std::size_t offset,
            std::size_t channel,
            std::uint16_t amplitude)
{
    amplitude = static_cast<std::uint16_t>((channel << 12) | (amplitude & 4095U));
    value.buffers[adc][offset + channel * 2] = static_cast<std::uint8_t>(amplitude);
    value.buffers[adc][offset + channel * 2 + 1] = static_cast<std::uint8_t>(amplitude >> 8);
}
} // namespace

int main()
{
    try
    {
        for (unsigned rotation = 0; rotation < 16; ++rotation)
        {
            std::array<std::uint16_t, 16> raw{}, decoded{};
            for (unsigned slot = 0; slot < 16; ++slot)
            {
                const auto channel = (slot + rotation) % 16;
                raw[slot] = static_cast<std::uint16_t>((channel << 12) | (channel * 113));
            }
            require(mimic::decodeAdcFrame(raw, decoded), "Valid channel rotation rejected");
            for (unsigned channel = 0; channel < 16; ++channel)
            {
                require(decoded[channel] == channel * 113,
                        "Channel identity changed with DMA phase");
            }
            raw[0] = raw[1];
            require(!mimic::decodeAdcFrame(raw, decoded), "Duplicate channel tag accepted");
        }
        auto prior = snapshot(1000000, 64, 64);
        auto current = snapshot(3000000, 128, 160);
        sample(current, 1, 64, 11, 700);
        sample(current, 1, 96, 12, 400);
        sample(current, 1, 160, 11, 4095); // Not completed: must not enter the current interval.
        auto result = mimic::analysePadSnapshot(prior, current);
        require(result.peaks[27] == 700 && result.peaks[28] == 400,
                "Completed-frame head/rim channel order differs");
        require(result.quality.usable(), "Normal cursor movement during copy was rejected");

        for (const std::size_t offset : {64U, 96U})
        {
            auto start = current.buffers[1].begin() + static_cast<std::ptrdiff_t>(offset);
            std::rotate(start, start + 30, start + 32);
        }
        result = mimic::analysePadSnapshot(prior, current);
        require(result.peaks[27] == 700 && result.peaks[28] == 400 && result.quality.usable(),
                "Calibration meter follows DMA position instead of physical channel");
        current.buffers[1][65] = current.buffers[1][67];
        require(mimic::analysePadSnapshot(prior, current).quality.invalidChannelFrames == 1,
                "Malformed channel tags were not rejected by calibration");

        prior = snapshot(4000000, 4064, 4064);
        current = snapshot(6000000, 32, 64);
        sample(current, 0, 4064, 2, 500);
        sample(current, 0, 0, 2, 900);
        result = mimic::analysePadSnapshot(prior, current);
        require(result.peaks[2] == 900 && result.quality.usable(),
                "Ring wrap changed the sample sequence");

        prior = snapshot(1000000, 64, 64);
        current = snapshot(3000000, 32, 64);
        require(mimic::analysePadSnapshot(prior, current).quality.copyOverlap == 2,
                "Writer overlap with emitted frames was not flagged");

        current = snapshot(11000000, 96, 96);
        require(mimic::analysePadSnapshot(prior, current).quality.pollGaps == 1,
                "A potentially ambiguous full-ring gap was not flagged");
        current = snapshot(3000000, 0, 96);
        require(mimic::analysePadSnapshot(prior, current).quality.invalidCursors == 2,
                "Unpublished ADC cursors were accepted");
        current = snapshot(3000000, 64, 64);
        require(mimic::analysePadSnapshot(prior, current).quality.stalledCursors == 2,
                "A stopped ADC cursor was accepted as usable waveform data");
        current = snapshot(3000000, 96, 96);
        sample(current, 0, 64, 4, 0xffff);
        result = mimic::analysePadSnapshot(prior, current);
        require(result.peaks[4] == 4095 && result.quality.clippedSamples == 1
                    && !result.quality.usable(),
                "12-bit masking or clipping detection differs");
        sample(current, 0, 64, 4, 4089);
        require(mimic::analysePadSnapshot(prior, current).quality.clippedSamples == 0,
                "Ordinary near-full-scale samples were rejected prematurely");
        sample(current, 0, 64, 4, 4090);
        require(mimic::analysePadSnapshot(prior, current).quality.clippedSamples == 1,
                "The conservative clipping margin differs");
        current.cursors[0].first = 95;
        require(mimic::analysePadSnapshot(prior, current).quality.invalidCursors == 1,
                "A misaligned cursor was accepted");
        current.cursors[0].first = 96;
        current.buffers[0].resize(4095);
        require(mimic::analysePadSnapshot(prior, current).quality.invalidCursors == 1,
                "A truncated DMA ring was accepted");

        std::cout << "Pad completed-frame intervals, wrap, copy overlap, gaps, bounds and clipping "
                     "passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
