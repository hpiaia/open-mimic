// SPDX-License-Identifier: AGPL-3.0-only
#include "SampleDecoder.h"

#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

void word(std::array<std::uint8_t, 1024>& block, std::size_t index, std::uint32_t value)
{
    for (unsigned byte = 0; byte < 4; ++byte)
    {
        block[index * 4 + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
    }
}
} // namespace

int main()
{
    try
    {
        std::array<std::uint8_t, 1024> block{};
        std::array<float, 4> output{};
        word(block, 0, (100U << 8) | 4U);
        word(block, 1, 4);
        // Signed deltas -1, +2, -8, +7 in the high four-bit fields.
        word(block, 2, 0xf2870000U);
        const auto info =
            mimic::decodeSampleBlock(block.data(), block.size(), output.data(), output.size());
        require(info.predictor == 100 && info.deltaBits == 4 && info.samples == 4,
                "Block header was not decoded");
        require(output[0] == 99.0f * 0x1p-23f && output[1] == 101.0f * 0x1p-23f
                    && output[2] == 93.0f * 0x1p-23f && output[3] == 100.0f * 0x1p-23f,
                "Signed prediction or PCM normalization differs");
        word(block, 0, 17);
        word(block, 1, 3);
        word(block, 2, 0x80003fffU);
        word(block, 3, 0xc0002000U);
        mimic::decodeSampleBlock(block.data(), block.size(), output.data(), output.size());
        require(output[0] == -65536.0f * 0x1p-23f && output[1] == -1.0f * 0x1p-23f
                    && output[2] == 0.0f,
                "Cross-word signed deltas differ");
        bool rejected = false;
        try
        {
            mimic::decodeSampleBlock(block.data(), 1023, output.data(), output.size());
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "A truncated compressed block was accepted");
        word(block, 0, 32);
        word(block, 1, 255);
        rejected = false;
        try
        {
            mimic::decodeSampleBlock(block.data(), block.size(), output.data(), output.size());
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        require(rejected, "An oversized decoded count was accepted");
        std::cout << "Sample decoder predictor, bit order, word crossings and bounds passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
