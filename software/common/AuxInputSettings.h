// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace mimic
{
struct AuxInputSettings
{
    float gain = 1.0f;
    bool followHeadphones = false;
};

inline float effectiveAuxGain(const AuxInputSettings& settings, float headphoneGain) noexcept
{
    if (!std::isfinite(settings.gain)
        || (settings.followHeadphones && !std::isfinite(headphoneGain)))
    {
        return 0.0f;
    }
    const auto aux = std::clamp(settings.gain, 0.0f, 1.0f);
    return settings.followHeadphones ? aux * std::clamp(headphoneGain, 0.0f, 1.0f) : aux;
}

// LM1973-compatible attenuation law. The board's serial contract matches this
// device; physical package identification is recorded as unconfirmed in research.
inline float auxAttenuationDb(int code) noexcept
{
    if (code < 32)
    {
        return static_cast<float>(code) * 0.5f;
    }
    if (code < 64)
    {
        return static_cast<float>(code - 16);
    }
    return static_cast<float>(code * 2 - 80);
}

inline std::uint8_t auxAttenuationCode(float gain) noexcept
{
    constexpr std::uint8_t mute = 127;
    if (!std::isfinite(gain) || gain <= 0.0f)
    {
        return mute;
    }
    const auto attenuation = -20.0f * std::log10(std::min(gain, 1.0f));
    if (attenuation > 76.0f)
    {
        return mute;
    }
    int closest = 0;
    auto distance = std::numeric_limits<float>::max();
    for (int code = 0; code <= 78; ++code)
    {
        const auto candidate = std::abs(auxAttenuationDb(code) - attenuation);
        if (candidate < distance)
        {
            closest = code;
            distance = candidate;
        }
    }
    return static_cast<std::uint8_t>(closest);
}
} // namespace mimic
