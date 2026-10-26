// SPDX-License-Identifier: AGPL-3.0-only
#include "InstrumentSelection.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace mimic
{
namespace
{
// Exact binary32 values of CFixedRandom::m_randNumbers at original ELF address
// 0x94eed8 (SHA-256 b12895171c9cb8fd7db067c816ba28b4ae4817869a34654175f3e6dfb683fa8c).
// Hexadecimal literals retain their original float bits. See the playback ledger.
constexpr std::array<float, 100> randomNumbers{
    0x1.56b1c80000000p-3f, 0x1.e0152c0000000p-2f, 0x1.7e5d6c0000000p-2f, 0x1.30b6060000000p-1f,
    0x1.7f75400000000p-2f, 0x1.b00e6a0000000p-1f, 0x1.0383b60000000p-1f, 0x1.22d5b20000000p-1f,
    0x1.6078f80000000p-3f, 0x1.fc338a0000000p-1f, 0x1.ef0b6a0000000p-2f, 0x1.c257c80000000p-2f,
    0x1.622c3c0000000p-1f, 0x1.d269fc0000000p-3f, 0x1.a44fa20000000p-1f, 0x1.cb42960000000p-1f,
    0x1.14494a0000000p-1f, 0x1.a640020000000p-1f, 0x1.8f32b20000000p-1f, 0x1.83f0d80000000p-1f,
    0x1.dbb2260000000p-1f, 0x1.a4699e0000000p-2f, 0x1.ad98280000000p-1f, 0x1.32a9e20000000p-3f,
    0x1.d3fc040000000p-3f, 0x1.e2dc1a0000000p-5f, 0x1.1fe1ba0000000p-1f, 0x1.73f4e20000000p-1f,
    0x1.0cb2a80000000p-2f, 0x1.56c9680000000p-3f, 0x1.477e380000000p-2f, 0x1.58dc8c0000000p-3f,
    0x1.8baa800000000p-1f, 0x1.84dfc40000000p-1f, 0x1.94e1300000000p-4f, 0x1.477d820000000p-2f,
    0x1.f8e3220000000p-1f, 0x1.e539000000000p-1f, 0x1.f7d14c0000000p-2f, 0x1.646a200000000p-1f,
    0x1.c2e5f80000000p-1f, 0x1.f1ac4a0000000p-5f, 0x1.2a89c20000000p-2f, 0x1.395a040000000p-2f,
    0x1.d996ce0000000p-1f, 0x1.d22d580000000p-2f, 0x1.2dee280000000p-2f, 0x1.eacec60000000p-1f,
    0x1.ffbe920000000p-1f, 0x1.a8af880000000p-2f, 0x1.0e1f3c0000000p-3f, 0x1.759c7e0000000p-1f,
    0x1.adbd7a0000000p-9f, 0x1.5fbd280000000p-1f, 0x1.418d7e0000000p-1f, 0x1.5ab70e0000000p-2f,
    0x1.d47fce0000000p-3f, 0x1.5bfbca0000000p-1f, 0x1.2631c20000000p-8f, 0x1.d14d1e0000000p-1f,
    0x1.7bbf4c0000000p-1f, 0x1.05e6a00000000p-2f, 0x1.8859e60000000p-1f, 0x1.5b1ccc0000000p-1f,
    0x1.232a280000000p-2f, 0x1.fda4960000000p-1f, 0x1.419c320000000p-2f, 0x1.c70e0e0000000p-4f,
    0x1.7e2a720000000p-3f, 0x1.86b0100000000p-1f, 0x1.a0fd300000000p-2f, 0x1.3f55440000000p-4f,
    0x1.d2511e0000000p-3f, 0x1.c163d20000000p-1f, 0x1.a325220000000p-2f, 0x1.96dddc0000000p-5f,
    0x1.ed51b40000000p-1f, 0x1.103c6e0000000p-1f, 0x1.b657b80000000p-1f, 0x1.b4609c0000000p-5f,
    0x1.211e960000000p-1f, 0x1.472fe60000000p-1f, 0x1.7b5e1c0000000p-2f, 0x1.d151940000000p-2f,
    0x1.5afaa40000000p-2f, 0x1.43e2d40000000p-1f, 0x1.c115fa0000000p-1f, 0x1.0106160000000p-1f,
    0x1.7d047e0000000p-3f, 0x1.a45b700000000p-1f, 0x1.d77b740000000p-1f, 0x1.0b938a0000000p-1f,
    0x1.bee0200000000p-1f, 0x1.cf768c0000000p-1f, 0x1.d75a3e0000000p-1f, 0x1.1551900000000p-1f,
    0x1.4ba3dc0000000p-1f, 0x1.c6b0f20000000p-1f, 0x1.c586bc0000000p-1f, 0x1.6a13140000000p-1f,
};
} // namespace

float FixedRandomSequence::next() noexcept
{
    cursor = (cursor + 1) % randomNumbers.size();
    return randomNumbers[cursor];
}

void FixedRandomSequence::reset() noexcept
{
    cursor = 0;
}

std::size_t FixedRandomSequence::position() const noexcept
{
    return cursor;
}

InstrumentSelection::InstrumentSelection(const std::vector<VelocityLayerDefinition>& definitions)
{
    if (definitions.size() > velocityMap.size())
    {
        throw std::invalid_argument("An articulation supports at most 128 velocity layers");
    }
    layers.reserve(definitions.size());
    for (const auto& definition : definitions)
    {
        if (definition.sampleIndices.size() > 16)
        {
            throw std::invalid_argument("A velocity layer supports at most 16 sample alternatives");
        }
        Layer layer;
        layer.range.minimumVelocity = definition.minimumVelocity;
        layer.count = definition.sampleIndices.size();
        for (std::size_t index = 0; index < layer.count; ++index)
        {
            if (definition.sampleIndices[index] < 0)
            {
                throw std::invalid_argument(
                    "A populated alternative must reference a valid sample");
            }
            layer.samples[index] = definition.sampleIndices[index];
            layer.queue[index] = index;
        }
        layers.push_back(layer);
    }
    int upperExclusive = 128;
    for (std::size_t remaining = layers.size(); remaining > 0; --remaining)
    {
        const auto index = remaining - 1;
        auto& layer = layers[index];
        const auto lower = std::clamp(layer.range.minimumVelocity, 0, 127);
        layer.range.maximumVelocity = upperExclusive - 1;
        for (int velocity = lower; velocity < upperExclusive; ++velocity)
        {
            velocityMap[static_cast<std::size_t>(velocity)] = index;
        }
        upperExclusive = lower;
    }
}

std::optional<std::size_t> InstrumentSelection::layerForVelocity(int velocity) const noexcept
{
    if (layers.empty())
    {
        return std::nullopt;
    }
    return velocityMap[static_cast<std::size_t>(std::clamp(velocity, 0, 127))];
}

const VelocityLayerRange& InstrumentSelection::rangeForLayer(std::size_t layer) const
{
    return layers.at(layer).range;
}

std::optional<InstrumentSampleSelection> InstrumentSelection::select(
    int velocity, FixedRandomSequence& random, bool forceFirstAlternative) noexcept
{
    const auto layerIndex = layerForVelocity(velocity);
    if (!layerIndex)
    {
        return std::nullopt;
    }
    auto& layer = layers[*layerIndex];
    if (layer.count == 0)
    {
        return std::nullopt;
    }
    std::size_t alternative = 0;
    if (layer.count <= 2)
    {
        layer.sequentialPosition = (layer.sequentialPosition + 1) % layer.count;
        alternative = layer.sequentialPosition;
    }
    else
    {
        const auto scaled = std::fma(random.next(), static_cast<float>(layer.count / 2), -0.5f);
        const auto position = static_cast<std::size_t>(
            std::clamp(static_cast<int>(scaled), 0, static_cast<int>(layer.count) - 1));
        alternative = layer.queue[position];
        std::move(layer.queue.begin() + static_cast<std::ptrdiff_t>(position + 1),
                  layer.queue.begin() + static_cast<std::ptrdiff_t>(layer.count),
                  layer.queue.begin() + static_cast<std::ptrdiff_t>(position));
        layer.queue[layer.count - 1] = alternative;
        layer.sequentialPosition = alternative;
    }
    if (forceFirstAlternative)
    {
        alternative = 0;
    }
    return InstrumentSampleSelection{*layerIndex, alternative, layer.samples[alternative]};
}

void InstrumentSelection::reset() noexcept
{
    for (auto& layer : layers)
    {
        layer.sequentialPosition = 0;
        for (std::size_t index = 0; index < layer.count; ++index)
        {
            layer.queue[index] = index;
        }
    }
}
} // namespace mimic
