// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

namespace mimic
{
// DMA frame indices confirmed by physical output listening tests.
inline constexpr int headphoneLeftChannel = 1;
inline constexpr int headphoneRightChannel = 3;
inline constexpr int masterLeftChannel = 7;
inline constexpr int masterRightChannel = 5;

constexpr bool isHeadphoneChannel(int channel) noexcept
{
    return channel == headphoneLeftChannel || channel == headphoneRightChannel;
}
} // namespace mimic
