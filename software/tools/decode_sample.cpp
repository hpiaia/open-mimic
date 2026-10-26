// SPDX-License-Identifier: AGPL-3.0-only
#include "SampleDecoder.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
std::uint64_t number(const char* text)
{
    const std::string value(text);
    std::size_t consumed = 0;
    const auto result = std::stoull(value, &consumed);
    if (value.empty() || value.front() == '-' || consumed != value.size())
    {
        throw std::invalid_argument("Expected a nonnegative byte offset or length");
    }
    return result;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc != 5)
        {
            std::cerr << "Usage: mimic-sample-decode INPUT OFFSET BYTES OUTPUT.f32\n";
            return 2;
        }
        const auto offset = number(argv[2]);
        const auto bytes = number(argv[3]);
        const auto size = std::filesystem::file_size(argv[1]);
        if (bytes % 1024 != 0 || offset > size || bytes > size - offset)
        {
            throw std::invalid_argument("Sample range is outside the file or not block aligned");
        }
        if (std::filesystem::exists(argv[4]))
        {
            throw std::runtime_error("Output already exists");
        }
        std::ifstream input(argv[1], std::ios::binary);
        std::ofstream output(argv[4], std::ios::binary);
        if (!input || !output)
        {
            throw std::runtime_error("Could not open input or output");
        }
        input.seekg(static_cast<std::streamoff>(offset));
        std::array<std::uint8_t, 1024> block{};
        std::array<float, 8128> decoded{};
        std::uint64_t samples = 0;
        for (std::uint64_t remaining = bytes; remaining != 0; remaining -= block.size())
        {
            if (!input.read(reinterpret_cast<char*>(block.data()), block.size()))
            {
                throw std::runtime_error("Truncated sample data");
            }
            const auto info = mimic::decodeSampleBlock(
                block.data(), block.size(), decoded.data(), decoded.size());
            for (std::size_t index = 0; index < info.samples; ++index)
            {
                std::uint32_t bits;
                static_assert(sizeof(bits) == sizeof(float));
                std::memcpy(&bits, &decoded[index], sizeof(bits));
                const std::array<char, 4> encoded{static_cast<char>(bits),
                                                  static_cast<char>(bits >> 8),
                                                  static_cast<char>(bits >> 16),
                                                  static_cast<char>(bits >> 24)};
                output.write(encoded.data(), encoded.size());
            }
            samples += info.samples;
        }
        if (!output)
        {
            throw std::runtime_error("Could not write decoded samples");
        }
        std::cout << samples << " scalar samples decoded\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
