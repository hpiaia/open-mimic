// SPDX-License-Identifier: AGPL-3.0-only
#include "ExclusiveOutputFile.h"

#include <cerrno>
#include <system_error>

namespace mimic
{
namespace
{
std::FILE* createExclusiveFile(const std::string& path)
{
    // C11 exclusive creation avoids closing and reopening the pathname.
    auto* file = std::fopen(path.c_str(), "wbx");
    if (file == nullptr)
    {
        throw std::system_error(errno, std::generic_category(), path);
    }
    return file;
}
} // namespace

ExclusiveOutputFile::ExclusiveOutputFile(const std::string& path)
    : file(createExclusiveFile(path)), buffer(file.get()), output(&buffer)
{
    output.exceptions(std::ios::badbit | std::ios::failbit);
}

std::ostream& ExclusiveOutputFile::stream() noexcept
{
    return output;
}

void ExclusiveOutputFile::close()
{
    if (file == nullptr)
    {
        return;
    }

    output.flush();
    auto* handle = file.release();
    buffer.detach();
    if (std::fclose(handle) != 0)
    {
        throw std::system_error(errno, std::generic_category(), "Closing capture file");
    }
}

void ExclusiveOutputFile::FileCloser::operator()(std::FILE* handle) const noexcept
{
    std::fclose(handle);
}

ExclusiveOutputFile::Buffer::Buffer(std::FILE* handle) noexcept : file(handle) {}

void ExclusiveOutputFile::Buffer::detach() noexcept
{
    file = nullptr;
}

std::streamsize ExclusiveOutputFile::Buffer::xsputn(const char* data, std::streamsize size)
{
    if (file == nullptr || size <= 0)
    {
        return 0;
    }

    return static_cast<std::streamsize>(std::fwrite(data, 1, static_cast<std::size_t>(size), file));
}

ExclusiveOutputFile::Buffer::int_type ExclusiveOutputFile::Buffer::overflow(int_type value)
{
    if (file == nullptr)
    {
        return traits_type::eof();
    }
    if (traits_type::eq_int_type(value, traits_type::eof()))
    {
        return traits_type::not_eof(value);
    }

    return std::fputc(traits_type::to_char_type(value), file) == EOF ? traits_type::eof() : value;
}

int ExclusiveOutputFile::Buffer::sync()
{
    return file == nullptr ? -1 : std::fflush(file);
}
} // namespace mimic
