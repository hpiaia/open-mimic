// SPDX-License-Identifier: AGPL-3.0-only
#include "Adc.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>

// Run with fake_adc.c preloaded; no physical ADC is used.
int main()
{
    const char* directory = std::getenv("OPEN_MIMIC_TEST_ADC_DIRECTORY");
    if (directory == nullptr)
    {
        throw std::runtime_error("Synthetic ADC directory is required");
    }
    mimic::Adc adc("/dev/ads7953.0");
    std::vector<std::uint8_t> expected(adc.layout().dmaBytes);
    for (std::size_t offset = 0; offset < expected.size(); offset += 32)
    {
        for (std::size_t slot = 0; slot < 16; ++slot)
        {
            const auto channel = (slot + 15) % 16;
            const auto word = static_cast<std::uint16_t>((channel << 12) | (offset / 32 + channel));
            std::memcpy(expected.data() + offset + slot * 2, &word, sizeof(word));
        }
    }
    const std::uint32_t cursor = 4064;
    {
        std::ofstream backing(std::string(directory) + "/adc0", std::ios::binary | std::ios::in);
        backing.write(reinterpret_cast<const char*>(expected.data()),
                      static_cast<std::streamsize>(expected.size()));
        backing.write(reinterpret_cast<const char*>(&cursor), sizeof(cursor));
        if (!backing)
        {
            throw std::runtime_error("Could not initialize synthetic ADC");
        }
    }
    std::vector<std::uint8_t> actual(expected.size());
    const auto cursors = adc.snapshot(actual);
    if (actual != expected || cursors.first != cursor || cursors.second != cursor)
    {
        throw std::runtime_error("Snapshot changed ring bytes or cursor values");
    }
    if (!adc.readCompletedFrames().valid)
    {
        throw std::runtime_error("Could not initialize frame cursor");
    }
    const std::uint32_t wrappedCursor = 64;
    {
        std::fstream backing(std::string(directory) + "/adc0",
                             std::ios::binary | std::ios::in | std::ios::out);
        backing.seekp(4096);
        backing.write(reinterpret_cast<const char*>(&wrappedCursor), sizeof(wrappedCursor));
    }
    const auto frames = adc.readCompletedFrames();
    if (!frames.valid || frames.count != 3)
    {
        throw std::runtime_error("Wrapped completed-frame interval differs");
    }
    for (std::size_t frame = 0; frame < frames.count; ++frame)
    {
        const auto offset = (4064 + frame * 32) % 4096;
        for (std::size_t channel = 0; channel < 16; ++channel)
        {
            if (frames.frames[frame][channel] != offset / 32 + channel)
            {
                throw std::runtime_error("Physical ADC channel changed across rotated DMA wrap");
            }
        }
    }
    if (adc.readCompletedFrames().count != 0)
    {
        throw std::runtime_error("Completed frames were duplicated");
    }
    adc.resetFrameReader();
    if (adc.readCompletedFrames().count != 0)
    {
        throw std::runtime_error("Reader reset replayed old frames");
    }
    for (const std::uint32_t invalid : {0U, 31U, 4097U})
    {
        std::fstream backing(std::string(directory) + "/adc0",
                             std::ios::binary | std::ios::in | std::ios::out);
        backing.seekp(4096);
        backing.write(reinterpret_cast<const char*>(&invalid), sizeof(invalid));
        backing.flush();
        const auto batch = adc.readCompletedFrames();
        if (batch.valid || batch.count != 0)
        {
            throw std::runtime_error("Invalid producer cursor accepted");
        }
    }
    actual.pop_back();
    try
    {
        adc.snapshot(actual);
    }
    catch (const std::invalid_argument&)
    {
        return 0;
    }
    throw std::runtime_error("Snapshot accepted an incorrect destination size");
}
