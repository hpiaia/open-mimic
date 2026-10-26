// SPDX-License-Identifier: AGPL-3.0-only
#include "MidiStream.h"

namespace mimic
{
namespace
{
std::size_t messageBytes(std::uint8_t status) noexcept
{
    if (status < 0xf0)
    {
        return (status & 0xe0) == 0xc0 ? 2 : 3;
    }
    switch (status)
    {
    case 0xf1:
    case 0xf3:
        return 2;
    case 0xf2:
        return 3;
    case 0xf6:
        return 1;
    default:
        return 0;
    }
}
} // namespace

void MidiStreamParser::emit(void* context, Callback callback) noexcept
{
    if (callback != nullptr)
    {
        callback(context, pending);
    }
    pending.size = 0;
}

void MidiStreamParser::push(std::uint8_t byte, void* context, Callback callback) noexcept
{
    if (byte >= 0xf8)
    {
        // Real-time bytes do not disturb running status or an in-flight SysEx.
        if (byte != 0xf9 && byte != 0xfd && callback != nullptr)
        {
            MidiMessage realtime;
            realtime.data[0] = byte;
            realtime.size = 1;
            callback(context, realtime);
        }
        return;
    }
    if (inSysEx)
    {
        if (byte < 0x80 || byte == 0xf7)
        {
            if (!droppingSysEx)
            {
                if (pending.size == pending.data.size())
                {
                    droppingSysEx = true;
                    ++discarded;
                }
                else
                {
                    pending.data[pending.size++] = byte;
                }
            }
            if (byte == 0xf7)
            {
                inSysEx = false;
                if (!droppingSysEx)
                {
                    emit(context, callback);
                }
                pending.size = 0;
                droppingSysEx = false;
            }
            return;
        }
        // A non-real-time status aborts an unfinished SysEx.
        if (!droppingSysEx)
        {
            ++discarded;
        }
        inSysEx = false;
        droppingSysEx = false;
        pending.size = 0;
    }
    if ((byte & 0x80) != 0)
    {
        if (pending.size > 1)
        {
            ++discarded;
        }
        pending.size = 0;
        runningStatus = byte < 0xf0 ? byte : 0;
        expectedBytes = messageBytes(byte);
        if (byte == 0xf0)
        {
            inSysEx = true;
            pending.data[pending.size++] = byte;
        }
        else if (expectedBytes != 0)
        {
            pending.data[pending.size++] = byte;
            if (expectedBytes == 1)
            {
                emit(context, callback);
            }
        }
        return;
    }
    if (pending.size == 0)
    {
        if (runningStatus == 0)
        {
            return;
        }
        pending.data[pending.size++] = runningStatus;
        expectedBytes = messageBytes(runningStatus);
    }
    pending.data[pending.size++] = byte;
    if (pending.size == expectedBytes)
    {
        emit(context, callback);
    }
}

void MidiStreamParser::reset() noexcept
{
    pending.size = 0;
    runningStatus = 0;
    expectedBytes = 0;
    inSysEx = false;
    droppingSysEx = false;
    discarded = 0;
}

std::uint64_t MidiStreamParser::discardedMessages() const noexcept
{
    return discarded;
}
} // namespace mimic
