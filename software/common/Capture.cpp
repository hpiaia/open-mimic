// SPDX-License-Identifier: AGPL-3.0-only
#include "Capture.h"

#include "ExclusiveOutputFile.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace mimic
{
namespace
{
template <class T> void writeLittleEndian(std::ostream& output, T value)
{
    for (unsigned i = 0; i < sizeof(T); ++i)
    {
        output.put(static_cast<char>((value >> (i * 8)) & 255));
    }
}
} // namespace

void writeCaptureHeader(std::ostream& output,
                        const std::array<AdcLayout, 2>& layouts,
                        std::uint32_t poll)
{
    output.write("OMADC002", 8);
    writeLittleEndian(output, poll);
    for (const auto& layout : layouts)
    {
        writeLittleEndian(output, layout.mapBytes);
        writeLittleEndian(output, layout.dmaBytes);
        writeLittleEndian(output, layout.portionBytes);
    }
}

void writeCaptureRecord(std::ostream& output,
                        std::uint64_t elapsed,
                        const std::array<std::pair<std::uint32_t, std::uint32_t>, 2>& cursors,
                        const std::array<std::vector<std::uint8_t>, 2>& buffers)
{
    writeLittleEndian(output, elapsed);
    for (const auto& cursor : cursors)
    {
        writeLittleEndian(output, cursor.first);
        writeLittleEndian(output, cursor.second);
    }
    for (const auto& buffer : buffers)
    {
        output.write(reinterpret_cast<const char*>(buffer.data()),
                     static_cast<std::streamsize>(buffer.size()));
    }
}

void capture(const std::string& path, unsigned seconds)
{
    if (seconds < 1 || seconds > 60)
    {
        throw std::invalid_argument("SECONDS must be 1..60");
    }
    Adc first("/dev/ads7953.0");
    Adc second("/dev/ads7953.1");
    ExclusiveOutputFile file(path);
    auto& output = file.stream();
    const std::array<AdcLayout, 2> layouts{first.layout(), second.layout()};
    constexpr std::uint32_t poll = 2000;
    writeCaptureHeader(output, layouts, poll);
    std::array<std::vector<std::uint8_t>, 2> buffers{
        std::vector<std::uint8_t>(layouts[0].dmaBytes),
        std::vector<std::uint8_t>(layouts[1].dmaBytes)};
    const auto start = std::chrono::steady_clock::now();
    std::uint64_t count = 0;
    std::uint64_t moved = 0;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds))
    {
        const std::array<std::pair<std::uint32_t, std::uint32_t>, 2> cursors{
            first.snapshot(buffers[0]), second.snapshot(buffers[1])};
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count();
        writeCaptureRecord(output, static_cast<std::uint64_t>(ns), cursors, buffers);
        if (cursors[0].first != cursors[0].second || cursors[1].first != cursors[1].second)
        {
            ++moved;
        }
        ++count;
        std::this_thread::sleep_for(std::chrono::microseconds(poll));
    }
    file.close();
    std::cerr << "Captured " << count << " snapshots; cursor moved during " << moved << '\n';
}
} // namespace mimic
