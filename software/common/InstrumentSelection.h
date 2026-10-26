// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace mimic
{
// One sequence belongs to an instrument and is shared across its articulations.
// The original advances it only for velocity layers with more than two takes.
class FixedRandomSequence final
{
public:
    float next() noexcept;
    void reset() noexcept;
    [[nodiscard]] std::size_t position() const noexcept;

private:
    std::size_t cursor = 0;
};

struct VelocityLayerDefinition
{
    int minimumVelocity = 0;
    std::vector<int> sampleIndices;
};

struct VelocityLayerRange
{
    int minimumVelocity = 0;
    int maximumVelocity = 127;
};

struct InstrumentSampleSelection
{
    std::size_t layerIndex = 0;
    std::size_t alternativeIndex = 0;
    int sampleIndex = -1;
};

// Artifact-backed selection primitives. Loading/readiness, voice generation,
// and hi-hat articulation blending are separate responsibilities.
class InstrumentSelection final
{
public:
    explicit InstrumentSelection(const std::vector<VelocityLayerDefinition>& definitions);

    [[nodiscard]] std::optional<std::size_t> layerForVelocity(int velocity) const noexcept;
    [[nodiscard]] const VelocityLayerRange& rangeForLayer(std::size_t layer) const;
    [[nodiscard]] std::optional<InstrumentSampleSelection>
    select(int velocity, FixedRandomSequence& random, bool forceFirstAlternative = false) noexcept;
    void reset() noexcept;

private:
    struct Layer
    {
        VelocityLayerRange range;
        std::array<int, 16> samples{};
        std::array<std::size_t, 16> queue{};
        std::size_t count = 0;
        std::size_t sequentialPosition = 0;
    };

    std::vector<Layer> layers;
    std::array<std::size_t, 128> velocityMap{};
};
} // namespace mimic
