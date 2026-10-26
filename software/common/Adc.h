// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mimic
{
struct AdcLayout
{
    std::uint32_t mapBytes = 0;
    std::uint32_t dmaBytes = 0;
    std::uint32_t portionBytes = 0;
    void validate() const;
};

struct AdcFrameBatch
{
    std::array<std::array<std::uint16_t, 16>, 128> frames{};
    std::size_t count = 0;
    bool valid = true;
};

class Adc final
{
public:
    explicit Adc(const std::string& path);
    ~Adc();
    Adc(const Adc&) = delete;
    Adc& operator=(const Adc&) = delete;

    [[nodiscard]] const AdcLayout& layout() const noexcept
    {
        return layout_;
    }

    AdcFrameBatch readCompletedFrames();

    void resetFrameReader() noexcept
    {
        frameCursor_ = 0;
    }

    std::pair<std::uint32_t, std::uint32_t> snapshot(std::vector<std::uint8_t>& destination) const;

private:
    [[maybe_unused]] int fd_ = -1;
    void* mapping_ = nullptr;
    AdcLayout layout_;
    std::uint32_t frameCursor_ = 0;
};
} // namespace mimic
