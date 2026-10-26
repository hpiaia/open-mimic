// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>

namespace mimic
{
struct SampleBlockInfo
{
    std::uint32_t samples = 0;
    std::int32_t predictor = 0;
    unsigned deltaBits = 0;
};

// The caller supplies one complete 1024-byte block and sufficient output space.
// No allocation occurs. Corrupt blocks throw on the loader/streaming worker.
SampleBlockInfo decodeSampleBlock(const std::uint8_t* source,
                                  std::size_t bytes,
                                  float* destination,
                                  std::size_t capacity);
} // namespace mimic
