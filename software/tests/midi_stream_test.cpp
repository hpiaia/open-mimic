// SPDX-License-Identifier: AGPL-3.0-only
#include "MidiStream.h"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
using Bytes = std::vector<std::uint8_t>;
using Events = std::vector<Bytes>;

void received(void* context, const mimic::MidiMessage& event) noexcept
{
    auto& events = *static_cast<Events*>(context);
    events.emplace_back(event.data.begin(), event.data.begin() + event.size);
}

void feed(mimic::MidiStreamParser& parser, Events& events, const Bytes& bytes)
{
    for (auto byte : bytes)
    {
        parser.push(byte, &events, received);
    }
}

void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
} // namespace

int main()
{
    try
    {
        mimic::MidiStreamParser parser;
        Events events;
        feed(parser, events, {0x99, 36, 0xf8, 100, 38, 0, 0xc9, 7, 8});
        require(events == Events{{0xf8}, {0x99, 36, 100}, {0x99, 38, 0}, {0xc9, 7}, {0xc9, 8}},
                "Running status, short messages, or interleaved clock differ");
        events.clear();
        feed(parser, events, {0xf2, 1, 0xfa, 2, 3, 4, 0xf1, 9, 0xf6});
        require(events == Events{{0xfa}, {0xf2, 1, 2}, {0xf1, 9}, {0xf6}},
                "System common messages must clear running status");
        events.clear();
        feed(parser, events, {0xf0, 0x7e, 0xfe, 1, 2, 0xf7, 0x99, 40, 90});
        require(events == Events{{0xfe}, {0xf0, 0x7e, 1, 2, 0xf7}, {0x99, 40, 90}},
                "Real-time messages corrupted SysEx framing");
        events.clear();
        feed(parser, events, {0xf0, 1, 0x99, 41, 91});
        require(events == Events{{0x99, 41, 91}} && parser.discardedMessages() == 1,
                "Interrupted SysEx must recover at the next status");
        events.clear();
        parser.push(0xf0, &events, received);
        for (std::size_t i = 0; i < mimic::MidiMessage::maximumBytes + 20; ++i)
        {
            parser.push(1, &events, received);
        }
        feed(parser, events, {0xf8, 0xf7, 0x89, 41, 0});
        require(events == Events{{0xf8}, {0x89, 41, 0}} && parser.discardedMessages() == 2,
                "Oversized SysEx must be dropped once and recover without overflow");
        parser.reset();
        events.clear();
        feed(parser, events, {36, 100, 0xf9, 0xfd});
        require(events.empty() && parser.discardedMessages() == 0,
                "Reset must discard stale status and undefined real-time bytes");
        for (std::uint8_t status = 0x80; status <= 0xe0; status += 0x10)
        {
            parser.reset();
            events.clear();
            Bytes message{static_cast<std::uint8_t>(status | 15), 42};
            if ((status & 0xe0) != 0xc0)
            {
                message.push_back(96);
            }
            feed(parser, events, message);
            feed(parser, events, Bytes(message.begin() + 1, message.end()));
            require(events == Events{message, message},
                    "A channel message did not retain its correct running-status length");
        }
        parser.reset();
        events.clear();
        Bytes maximum(mimic::MidiMessage::maximumBytes, 1);
        maximum.front() = 0xf0;
        maximum.back() = 0xf7;
        feed(parser, events, maximum);
        require(events == Events{maximum} && parser.discardedMessages() == 0,
                "A maximum-size complete SysEx should fit without truncation");
        std::cout << "MIDI running status, real-time, system common and bounded SysEx passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
