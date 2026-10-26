// SPDX-License-Identifier: AGPL-3.0-only
#include "HatControl.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace mimic
{
HatControlTable::HatControlTable(const std::vector<int>& articulationIndices)
{
    if (articulationIndices.size() < 2 || articulationIndices.size() > 32)
    {
        throw std::invalid_argument("A hi-hat control zone needs 2..32 articulations");
    }
    for (const auto index : articulationIndices)
    {
        if (index < 0)
        {
            throw std::invalid_argument("A hi-hat control zone references a missing articulation");
        }
    }
    for (std::size_t bin = 0; bin < entries.size(); ++bin)
    {
        auto& entry = entries[bin];
        entry.behaviorFlags = {
            bin <= 85, bin >= 40 && bin <= 50, bin <= 85, bin >= 70 && bin <= 85};
        int blendStart = 0;
        int blendEnd = 0;
        if (bin <= 15)
        {
            entry.firstControlOrdinal = 0;
        }
        else if (bin <= 39)
        {
            entry.firstControlOrdinal = 1;
        }
        else if (bin <= 50)
        {
            entry.firstControlOrdinal = 1;
            entry.secondControlOrdinal = 2;
            blendStart = 40;
            blendEnd = 50;
        }
        else if (bin <= 69)
        {
            entry.firstControlOrdinal = 2;
        }
        else if (bin <= 85)
        {
            entry.firstControlOrdinal = 2;
            entry.secondControlOrdinal = 3;
            blendStart = 70;
            blendEnd = 85;
        }
        else if (bin <= 89)
        {
            entry.firstControlOrdinal = 3;
        }
        else if (bin <= 95)
        {
            entry.firstControlOrdinal = 3;
            entry.secondControlOrdinal = 4;
            blendStart = 90;
            blendEnd = 95;
        }
        else
        {
            entry.firstControlOrdinal = 4;
        }
        const auto resolve = [&articulationIndices](int ordinal)
        {
            const auto bounded =
                std::min(static_cast<std::size_t>(ordinal), articulationIndices.size() - 1);
            return articulationIndices[bounded];
        };
        entry.firstArticulation = resolve(entry.firstControlOrdinal);
        entry.twoVoices = entry.secondControlOrdinal >= 0;
        if (entry.twoVoices)
        {
            entry.secondArticulation = resolve(entry.secondControlOrdinal);
            const auto fraction = static_cast<float>(static_cast<int>(bin) - blendStart)
                                  / static_cast<float>(blendEnd - blendStart);
            entry.firstGain = std::sqrt(1.0f - fraction);
            // The first transition deliberately has a linear second gain.
            // The later two transitions use equal-power square-root gains.
            entry.secondGain = blendStart == 40 ? fraction : std::sqrt(fraction);
        }
    }
}

const HatControlEntry& HatControlTable::entryForOpenness(float openness) const noexcept
{
    if (std::isnan(openness))
    {
        openness = 0.0f;
    }
    const auto bounded = std::clamp(openness, 0.0f, 1.0f);
    const auto bin = std::min(static_cast<std::size_t>(bounded * 100.0f), entries.size() - 1);
    return entries[bin];
}

const HatControlEntry& HatControlTable::entryForBin(std::size_t bin) const
{
    return entries.at(bin);
}
} // namespace mimic
