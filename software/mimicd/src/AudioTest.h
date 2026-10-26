// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioRouting.h"

#include <cstdint>

namespace mimic
{
struct AudioTestOptions
{
    // Default to the physical headphone jack.
    std::uint16_t channelMask = (1U << headphoneLeftChannel) | (1U << headphoneRightChannel);
    double frequencyHz = 440.0;
    double durationSeconds = 3.0;
    float amplitude = 1.0f;
};

int audioToneSelfTest();
int audioProbe();
int stopAudioTest();
int runAudioTest(const AudioTestOptions& options);
} // namespace mimic
