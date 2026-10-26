// SPDX-License-Identifier: AGPL-3.0-only
#include "Adc.h"

#include <cerrno>
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

std::pair<std::uint32_t, std::uint32_t> Adc::snapshot(std::vector<std::uint8_t>& destination) const
{
    if (destination.size() != layout_.dmaBytes)
    {
        throw std::invalid_argument("Wrong ADC snapshot size");
    }
    const auto* bytes = static_cast<volatile const std::uint8_t*>(mapping_);
    const auto* cursor = reinterpret_cast<volatile const std::uint32_t*>(bytes + layout_.dmaBytes);
    const auto before = *cursor;
    for (std::size_t i = 0; i < destination.size(); ++i)
    {
        destination[i] = bytes[i];
    }
    return {before, *cursor}; // Diagnostic snapshot; not atomic or guaranteed lossless.
}
} // namespace mimic
