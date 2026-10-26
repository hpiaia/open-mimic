// SPDX-License-Identifier: AGPL-3.0-only
#include "Adc.h"

#include "AdcFrame.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>
#ifdef __linux__
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace mimic
{
namespace
{
constexpr std::uint32_t maximumDmaBytes = 1024 * 1024;
constexpr std::uint32_t adcFrameBytes = 32;
#ifdef __linux__
constexpr unsigned long mapSizeIoctl = 0x4101;
constexpr unsigned long dmaSizeIoctl = 0x4102;
constexpr unsigned long portionSizeIoctl = 0x4103;
#endif
} // namespace

void AdcLayout::validate() const
{
    if (dmaBytes == 0 || dmaBytes > maximumDmaBytes || dmaBytes % adcFrameBytes != 0
        || mapBytes < dmaBytes + sizeof(std::uint32_t) || mapBytes > 2 * maximumDmaBytes
        || portionBytes == 0 || portionBytes > dmaBytes)
    {
        throw std::runtime_error("Unexpected ADC mapping layout");
    }
}

Adc::Adc(const std::string& path)
{
#ifdef __linux__
    fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd_ < 0)
    {
        throw std::system_error(errno, std::generic_category(), path);
    }
    try
    {
        const auto query = [this](unsigned long request)
        {
            // Pearl returns sizes directly, rather than through an output pointer.
            const int value = ::ioctl(fd_, request);
            if (value < 0)
            {
                throw std::system_error(errno, std::generic_category(), "ADC ioctl");
            }
            return static_cast<std::uint32_t>(value);
        };
        layout_ = {query(mapSizeIoctl), query(dmaSizeIoctl), query(portionSizeIoctl)};
        layout_.validate();
        mapping_ = ::mmap(nullptr, layout_.mapBytes, PROT_READ, MAP_PRIVATE, fd_, 0);
        if (mapping_ == MAP_FAILED)
        {
            mapping_ = nullptr;
            throw std::system_error(errno, std::generic_category(), "ADC mmap");
        }
    }
    catch (...)
    {
        ::close(fd_);
        fd_ = -1;
        throw;
    }
#else
    (void)path;
    throw std::runtime_error("ADC access requires Linux");
#endif
}

Adc::~Adc()
{
#ifdef __linux__
    if (mapping_)
    {
        ::munmap(mapping_, layout_.mapBytes);
    }
    if (fd_ >= 0)
    {
        ::close(fd_);
    }
#endif
}

AdcFrameBatch Adc::readCompletedFrames()
{
    if (layout_.dmaBytes != 4096 || layout_.portionBytes != 32)
    {
        throw std::runtime_error("Completed-frame reader requires a 4096-byte ADC ring");
    }
    const auto* words = static_cast<volatile const std::uint32_t*>(mapping_);
    const auto* cursor = words + 1024;
    const auto before = *cursor;
    const auto validCursor = [](std::uint32_t value)
    { return value > 0 && value <= 4096 && value % 32 == 0; };
    AdcFrameBatch result;
    if (!validCursor(before))
    {
        frameCursor_ = 0;
        result.valid = false;
        return result;
    }
    if (frameCursor_ == 0)
    {
        frameCursor_ = before;
        return result;
    }
    const auto produced = (before + 4096 - frameCursor_) % 4096;
    auto offset = frameCursor_ % 4096;
    result.count = produced / 32;
    for (std::size_t frame = 0; frame < result.count; ++frame)
    {
        for (std::size_t word = 0; word < 8; ++word)
        {
            const auto value = words[offset / 4 + word];
            std::memcpy(result.frames[frame].data() + word * 2, &value, sizeof(value));
        }
        std::array<std::uint16_t, 16> channels{};
        if (!decodeAdcFrame(result.frames[frame], channels))
        {
            result.count = 0;
            result.valid = false;
            frameCursor_ = 0;
            return result;
        }
        result.frames[frame] = channels;
        offset = (offset + 32) % 4096;
    }
    const auto after = *cursor;
    if (!validCursor(after) || produced + (after + 4096 - before) % 4096 >= 4096)
    {
        result.count = 0;
        result.valid = false;
        frameCursor_ = 0;
    }
    else
    {
        frameCursor_ = before;
    }
    // Caller must reject scheduling/copy gaps >= one ring's observed duration.
    // Modulo producer cursors alone cannot identify an unobserved full wrap.
    return result;
}

std::pair<std::uint32_t, std::uint32_t> Adc::snapshot(std::vector<std::uint8_t>& destination) const
{
    if (destination.size() != layout_.dmaBytes)
    {
        throw std::invalid_argument("Wrong ADC snapshot size");
    }
    // mmap is page aligned and validated DMA lengths are multiples of 32 bytes.
    // Read the shared DMA mapping with aligned bus words: byte-wide volatile
    // accesses are particularly expensive on the original uncached ARM mapping.
    const auto* words = static_cast<volatile const std::uint32_t*>(mapping_);
    const auto wordCount = destination.size() / sizeof(std::uint32_t);
    const auto* cursor = words + wordCount;
    const auto before = *cursor;
    for (std::size_t i = 0; i < wordCount; ++i)
    {
        const std::uint32_t word = words[i];
        // Preserve the mapping's native byte order without imposing alignment
        // or uint32_t aliasing requirements on the byte-vector destination.
        std::memcpy(destination.data() + i * sizeof(word), &word, sizeof(word));
    }
    return {before, *cursor}; // Diagnostic snapshot; not atomic or guaranteed lossless.
}
} // namespace mimic
