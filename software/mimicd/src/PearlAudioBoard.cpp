// SPDX-License-Identifier: AGPL-3.0-only
#include "PearlAudioBoard.h"

#include <cerrno>
#include <string>
#include <system_error>

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace mimic
{
#if defined(__linux__)
namespace
{
void writeControlFile(const std::string& path, const std::string& value)
{
    const auto descriptor = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (descriptor < 0)
    {
        throw std::system_error(errno, std::generic_category(), "Opening " + path);
    }
    const auto written = ::write(descriptor, value.data(), value.size());
    const auto error = errno;
    ::close(descriptor);
    if (written != static_cast<ssize_t>(value.size()))
    {
        throw std::system_error(
            written < 0 ? error : EIO, std::generic_category(), "Writing " + path);
    }
}

void enableBoardOutput(int number)
{
    const auto path = "/sys/class/gpio/gpio" + std::to_string(number);
    if (::access(path.c_str(), F_OK) != 0)
    {
        try
        {
            writeControlFile("/sys/class/gpio/export", std::to_string(number));
        }
        catch (const std::system_error& error)
        {
            if (error.code().value() != EBUSY)
            {
                throw;
            }
        }
    }
    writeControlFile(path + "/direction", "high");
}
} // namespace
#endif

void enablePearlAnalogOutputs()
{
#if defined(__linux__)
    // Original CMainPane order: GPIO6.16 followed by GPIO5.3.
    enableBoardOutput(176);
    enableBoardOutput(131);
#endif
}
} // namespace mimic
