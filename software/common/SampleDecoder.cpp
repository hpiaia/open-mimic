// SPDX-License-Identifier: AGPL-3.0-only
#include "SampleDecoder.h"

#include <cstring>
#include <stdexcept>

namespace mimic
{
namespace
{
std::uint32_t readLittleEndian(const std::uint8_t* source) noexcept
{
    return static_cast<std::uint32_t>(source[0]) | (static_cast<std::uint32_t>(source[1]) << 8)
           | (static_cast<std::uint32_t>(source[2]) << 16)
           | (static_cast<std::uint32_t>(source[3]) << 24);
}

std::int32_t signedBits(std::uint32_t bits) noexcept
{
    std::int32_t result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}
} // namespace

SampleBlockInfo decodeSampleBlock(const std::uint8_t* source,
                                  std::size_t bytes,
                                  float* destination,
                                  std::size_t capacity)
{
    if (source == nullptr || bytes != 1024)
    {
        throw std::invalid_argument("A Mimic sample block must contain 1024 bytes");
    }
    const auto header = readLittleEndian(source);
    const auto count = readLittleEndian(source + 4);
    const auto width = header & 255U;
    auto previous = header >> 8;
    if ((previous & 0x800000U) != 0)
    {
        previous |= 0xff000000U;
    }
    const SampleBlockInfo info{count, signedBits(previous), width};
    if (count == 0)
    {
        return info;
    }
    if (width == 0 || width > 32 || count > 8128U / width)
    {
        throw std::runtime_error("Invalid Mimic sample block header");
    }
    if (destination == nullptr || capacity < count)
    {
        throw std::invalid_argument("Sample block output buffer is too small");
    }
    const auto mask = width == 32 ? 0xffffffffU : (1U << width) - 1U;
    std::size_t position = 0;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto word = position / 32;
        const auto offset = static_cast<unsigned>(position % 32);
        const auto available = 32U - offset;
        const auto first = readLittleEndian(source + 8 + word * 4);
        std::uint32_t delta;
        if (width <= available)
        {
            delta = (first >> (available - width)) & mask;
        }
        else
        {
            const auto second = readLittleEndian(source + 8 + (word + 1) * 4);
            const auto combined = (static_cast<std::uint64_t>(first) << 32) | second;
            delta = static_cast<std::uint32_t>(combined >> (64U - offset - width)) & mask;
        }
        if (width < 32 && (delta & (1U << (width - 1))) != 0)
        {
            delta |= ~mask;
        }
        // ARM adds modulo 2^32; unsigned arithmetic avoids signed overflow.
        previous += delta;
        destination[index] = static_cast<float>(signedBits(previous)) * 0x1p-23f;
        position += width;
    }
    return info;
}
} // namespace mimic
