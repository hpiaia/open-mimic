// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>

namespace mimic
{
struct PadRoute
{
    const char* id;
    const char* name;
    int head;
    int rim;
    int defaultZones;
    const char* sd3Instrument;
    int headNote;
    int rimNote;
    int rimshotNote;
};

// Physical ADS7953 channel IDs, never DMA positions. Global ID = ADC * 16 + channel.
// Unmeasured inputs remain unassigned; adding a hardware input requires a code change.
inline constexpr std::array<PadRoute, 11> padRoutes{{
    {"kick", "Kick", 18, 19, 1, "Kick", 36, -1, -1},
    {"snare", "Snare", 16, 17, 2, "Snare", 38, 71, 40},
    {"tom1", "Tom 1", 26, 27, 2, "Rack Tom 2", 47, 79, 80},
    {"tom2", "Tom 2", 28, 29, 2, "Rack Tom 3", 45, 77, 78},
    {"tom3", "Tom 3", 30, 31, 2, "Floor Tom 1", 43, 74, 75},
    {"tom4", "Tom 4", 0, 1, 2, "Floor Tom 2", 41, 72, 73},
    {"aux1", "Aux 1", -1, -1, 1, "Unassigned", -1, -1, -1},
    {"aux2", "Aux 2", 8, 9, 1, "Rack Tom 1", 48, 81, 82},
    {"aux3", "Aux 3", -1, -1, 1, "Unassigned", -1, -1, -1},
    {"aux4", "Aux 4", -1, -1, 1, "Unassigned", -1, -1, -1},
    {"aux5", "Aux 5", -1, -1, 1, "Unassigned", -1, -1, -1},
}};
} // namespace mimic
