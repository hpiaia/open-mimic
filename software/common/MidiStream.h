// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mimic
{
struct MidiMessage
{
    static constexpr std::size_t maximumBytes = 1024;
    std::array<std::uint8_t, maximumBytes> data{};
    std::size_t size = 0;
};

// MIDI 1.0 wire parser: running status, interleaved real-time messages, and
// bounded SysEx. Callback data is valid until the next input byte.
class MidiStreamParser final
{
public:
    using Callback = void (*)(void* context, const MidiMessage& message) noexcept;

    void push(std::uint8_t byte, void* context, Callback callback) noexcept;
    void reset() noexcept;
    [[nodiscard]] std::uint64_t discardedMessages() const noexcept;

private:
    void emit(void* context, Callback callback) noexcept;
    MidiMessage pending;
    std::uint8_t runningStatus = 0;
    std::size_t expectedBytes = 0;
    std::uint64_t discarded = 0;
    bool inSysEx = false;
    bool droppingSysEx = false;
};
} // namespace mimic
