// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstdint>

namespace mimic
{
// ADS7953 conversion words carry the physical channel in bits 15:12.
// DMA frame position may rotate on restart and is never a channel identity.
inline bool decodeAdcFrame(const std::array<std::uint16_t, 16>& raw,
                           std::array<std::uint16_t, 16>& channels) noexcept
{
    unsigned seen = 0;
    for (const auto word : raw)
    {
        const auto channel = word >> 12;
        const auto bit = 1U << channel;
        if ((seen & bit) != 0)
        {
            return false;
        }
        seen |= bit;
        channels[channel] = word & 4095U;
    }
    return seen == 65535U;
}
} // namespace mimic
