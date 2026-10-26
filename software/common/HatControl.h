// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace mimic
{
struct HatControlEntry
{
    int firstArticulation = -1;
    int secondArticulation = -1;
    float firstGain = 1.0f;
    float secondGain = 1.0f;
    bool twoVoices = false;
    int firstControlOrdinal = -1;
    int secondControlOrdinal = -1;

    // Original table bytes at record+0x28..0x2b. Their downstream roles have
    // not all been resolved; retain them without inventing semantic names.
    std::array<bool, 4> behaviorFlags{};
};

// One table per logical hi-hat control zone. Ordered articulation indices
// must already be resolved from the metadata's control-zone articulation IDs.
// This models CHatCtrlTab::BuildForZone3, not pedal motion or voice muting.
class HatControlTable final
{
public:
    explicit HatControlTable(const std::vector<int>& articulationIndices);

    [[nodiscard]] const HatControlEntry& entryForOpenness(float openness) const noexcept;
    [[nodiscard]] const HatControlEntry& entryForBin(std::size_t bin) const;

private:
    std::array<HatControlEntry, 100> entries{};
};
} // namespace mimic
