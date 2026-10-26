// SPDX-License-Identifier: AGPL-3.0-only
#include "InstrumentSelection.h"

#include <array>
#include <iostream>
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

mimic::InstrumentSelection alternatives(int count)
{
    mimic::VelocityLayerDefinition layer;
    for (int index = 0; index < count; ++index)
    {
        layer.sampleIndices.push_back(100 + index);
    }
    return mimic::InstrumentSelection({layer});
}

void verifyVelocityBoundaries()
{
    mimic::InstrumentSelection selector({{0, {0}}, {30, {1}}, {60, {2}}, {87, {3}}, {107, {4}}});
    const std::array<int, 5> lower{0, 30, 60, 87, 107};
    const std::array<int, 5> upper{29, 59, 86, 106, 127};
    for (std::size_t layer = 0; layer < lower.size(); ++layer)
    {
        require(selector.layerForVelocity(lower[layer]) == layer, "Layer lower boundary changed");
        require(selector.layerForVelocity(upper[layer]) == layer, "Layer upper boundary changed");
        require(selector.rangeForLayer(layer).maximumVelocity == upper[layer],
                "Derived layer upper bound changed");
    }
    require(selector.layerForVelocity(-100) == 0, "Negative velocity must clamp to zero");
    require(selector.layerForVelocity(1000) == 4, "High velocity must clamp to127");
    mimic::InstrumentSelection duplicate({{0, {0}}, {30, {1}}, {30, {2}}});
    require(duplicate.layerForVelocity(30) == 2, "Duplicate thresholds must keep the upper layer");
}

void verifySmallAlternativeSets()
{
    mimic::FixedRandomSequence random;
    auto single = alternatives(1);
    auto pair = alternatives(2);
    for (int index = 0; index < 8; ++index)
    {
        require(single.select(64, random)->sampleIndex == 100, "One take must remain fixed");
        require(pair.select(64, random)->alternativeIndex
                    == static_cast<std::size_t>((index + 1) % 2),
                "Two takes must alternate, starting with index1");
    }
    require(random.position() == 0, "Small alternative sets must not consume random values");
    pair.reset();
    require(pair.select(64, random, true)->alternativeIndex == 0, "Forced-first selection changed");
    require(pair.select(64, random)->alternativeIndex == 0,
            "Forced-first selection must still advance the underlying state");
}

void verifyGoldenSelections()
{
    // Derived from the original table and queue operations, not an acoustic trace.
    const std::array<int, 20> goldenFour{0, 1, 2, 3, 1, 0, 2, 3, 0, 1,
                                         2, 3, 0, 2, 3, 1, 2, 3, 1, 2};
    const std::array<int, 20> goldenSixteen{3,  2, 6, 4, 10, 7,  9, 0, 15, 11,
                                            12, 3, 5, 4, 10, 14, 9, 7, 0,  11};
    mimic::FixedRandomSequence randomFour;
    mimic::FixedRandomSequence randomSixteen;
    auto four = alternatives(4);
    auto sixteen = alternatives(16);
    for (std::size_t index = 0; index < goldenFour.size(); ++index)
    {
        require(four.select(64, randomFour)->sampleIndex == 100 + goldenFour[index],
                "Four-take golden sequence differs from the original algorithm");
        require(sixteen.select(64, randomSixteen)->sampleIndex == 100 + goldenSixteen[index],
                "Sixteen-take golden sequence differs from the original algorithm");
    }
}

void verifyHistoryAndSharedSequence()
{
    for (int count = 3; count <= 16; ++count)
    {
        auto selector = alternatives(count);
        mimic::FixedRandomSequence random;
        std::vector<std::size_t> history;
        for (int hit = 0; hit < 500; ++hit)
        {
            const auto selection = selector.select(64, random);
            require(selection.has_value(), "Valid alternative set returned no sample");
            const auto recent = static_cast<std::size_t>((count + 1) / 2);
            const auto begin = history.size() > recent ? history.size() - recent : 0;
            for (auto index = begin; index < history.size(); ++index)
            {
                require(history[index] != selection->alternativeIndex,
                        "Recent-take exclusion was not preserved");
            }
            history.push_back(selection->alternativeIndex);
        }
        require(random.position() == 0, "The fixed random table must wrap every100 selections");
    }
    auto first = alternatives(4);
    auto second = alternatives(4);
    mimic::FixedRandomSequence shared;
    require(first.select(64, shared).has_value(), "First articulation failed to select");
    require(second.select(64, shared).has_value(), "Second articulation failed to select");
    require(shared.position() == 2, "Articulations must share the instrument random position");
    first.reset();
    require(shared.position() == 2,
            "Resetting one articulation must not reset the shared sequence");
}

void verifyEmptyAndInvalidInputs()
{
    mimic::FixedRandomSequence random;
    mimic::InstrumentSelection empty(std::vector<mimic::VelocityLayerDefinition>{});
    require(!empty.select(64, random), "An empty articulation must not select a sample");
    mimic::InstrumentSelection emptyLayer(std::vector<mimic::VelocityLayerDefinition>{{0, {}}});
    require(!emptyLayer.select(64, random), "An empty layer must not select a sample");
    bool rejected = false;
    try
    {
        auto oversized = alternatives(17);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    require(rejected, "More than16 alternatives must be rejected before runtime selection");
}
} // namespace

int main()
{
    try
    {
        verifyVelocityBoundaries();
        verifySmallAlternativeSets();
        verifyGoldenSelections();
        verifyHistoryAndSharedSequence();
        verifyEmptyAndInvalidInputs();
        std::cout << "Instrument velocity and alternative selection checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
