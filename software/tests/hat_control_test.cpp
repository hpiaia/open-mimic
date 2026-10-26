// SPDX-License-Identifier: AGPL-3.0-only
#include "HatControl.h"
#include "fixtures/hat_control_original_table.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void verifyOriginalTable()
{
    const std::vector<int> tipArticulations{6, 7, 8, 9, 10};
    const mimic::HatControlTable table(tipArticulations);
    for (std::size_t bin = 0; bin < originalHatEntries.size(); ++bin)
    {
        const auto& expected = originalHatEntries[bin];
        const auto& actual = table.entryForBin(bin);
        require(actual.firstControlOrdinal == expected.first, "First control ordinal changed");
        require(actual.secondControlOrdinal == expected.second, "Second control ordinal changed");
        require(actual.firstArticulation == tipArticulations[expected.first],
                "Control ordinal must resolve to the supplied articulation index");
        require(actual.secondArticulation
                    == (expected.second < 0 ? -1 : tipArticulations[expected.second]),
                "Second control articulation resolution changed");
        require(actual.firstGain == expected.firstGain, "First gain differs from original ARM");
        require(actual.secondGain == expected.secondGain, "Second gain differs from original ARM");
        require(actual.twoVoices == expected.twoVoices, "Two-voice transition boundaries changed");
        require(actual.behaviorFlags == expected.flags, "Original behavior flags changed");
        const auto midpoint = (static_cast<float>(bin) + 0.5f) / 100.0f;
        require(&table.entryForOpenness(midpoint) == &actual,
                "Continuous openness must select the original hundredth bin");
    }
    const auto& asymmetric = table.entryForBin(45);
    require(asymmetric.firstGain != asymmetric.secondGain,
            "The first transition must retain its asymmetric gain law");
}

void verifyBoundsAndShortZones()
{
    const mimic::HatControlTable shortZone({40, 41});
    require(shortZone.entryForBin(99).firstArticulation == 41,
            "High ordinals must clamp to the last available articulation");
    require(shortZone.entryForBin(75).secondArticulation == 41,
            "Blended ordinals must also clamp in short control zones");
    require(&shortZone.entryForOpenness(-1.0f) == &shortZone.entryForBin(0),
            "Negative openness must clamp to bin zero");
    require(&shortZone.entryForOpenness(1.0f) == &shortZone.entryForBin(99),
            "Fully open must select bin99 rather than100");
    require(&shortZone.entryForOpenness(std::numeric_limits<float>::infinity())
                == &shortZone.entryForBin(99),
            "Infinite openness must be bounded");
    require(&shortZone.entryForOpenness(std::numeric_limits<float>::quiet_NaN())
                == &shortZone.entryForBin(0),
            "Invalid external controller values must have a defined safe result");
    bool rejected = false;
    try
    {
        const mimic::HatControlTable invalid({1});
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected, "An unbuildable single-articulation control zone must be rejected");
}
} // namespace

int main()
{
    try
    {
        verifyOriginalTable();
        verifyBoundsAndShortZones();
        std::cout << "All 100 original hi-hat table entries matched\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
