// SPDX-License-Identifier: AGPL-3.0-only
#include "InstrumentSelection.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
int argument(const char* value, int minimum, int maximum)
{
    std::size_t consumed = 0;
    const std::string text(value);
    const auto result = std::stoi(text, &consumed);
    if (consumed != text.size() || result < minimum || result > maximum)
    {
        throw std::invalid_argument("Selection trace argument is outside its allowed range");
    }
    return result;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc >= 3 && std::string(argv[1]) == "--velocity")
        {
            std::vector<mimic::VelocityLayerDefinition> layers;
            for (int index = 2; index < argc; ++index)
            {
                layers.push_back({argument(argv[index], -1000, 1000), {0}});
            }
            mimic::InstrumentSelection selector(layers);
            for (int velocity = 0; velocity < 128; ++velocity)
            {
                std::cout << "M " << velocity << ' ' << *selector.layerForVelocity(velocity)
                          << '\n';
            }
            for (std::size_t layer = 0; layer < layers.size(); ++layer)
            {
                const auto& range = selector.rangeForLayer(layer);
                std::cout << "R " << layer << ' ' << range.minimumVelocity << ' '
                          << range.maximumVelocity << '\n';
            }
            return 0;
        }
        if (argc != 4)
        {
            std::cerr << "Usage: instrument-selection-trace ALTERNATIVES HITS FORCE_INTERVAL\n"
                         "       instrument-selection-trace --velocity MINIMUM [MINIMUM ...]\n";
            return 2;
        }
        const auto count = argument(argv[1], 1, 16);
        const auto hits = argument(argv[2], 1, 100000);
        const auto forcedInterval = argument(argv[3], 0, hits);
        mimic::VelocityLayerDefinition layer;
        for (int index = 0; index < count; ++index)
        {
            layer.sampleIndices.push_back(index);
        }
        mimic::InstrumentSelection selector(std::vector<mimic::VelocityLayerDefinition>{layer});
        mimic::FixedRandomSequence random;
        for (int hit = 0; hit < hits; ++hit)
        {
            const auto force = forcedInterval > 0 && hit % forcedInterval == 0;
            const auto selection = selector.select(64, random, force);
            if (!selection)
            {
                throw std::runtime_error("Valid fixture failed to select a sample");
            }
            std::cout << selection->sampleIndex << '\n';
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
