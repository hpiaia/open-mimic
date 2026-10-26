// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstdio>
#include <memory>
#include <ostream>
#include <streambuf>
#include <string>

namespace mimic
{
// Keeps the exclusively created file open for its entire write lifetime.
class ExclusiveOutputFile final
{
public:
    explicit ExclusiveOutputFile(const std::string& path);
    ~ExclusiveOutputFile() = default;

    ExclusiveOutputFile(const ExclusiveOutputFile&) = delete;
    ExclusiveOutputFile& operator=(const ExclusiveOutputFile&) = delete;

    [[nodiscard]] std::ostream& stream() noexcept;
    void close();

private:
    struct FileCloser
    {
        void operator()(std::FILE* file) const noexcept;
    };

    class Buffer final : public std::streambuf
    {
    public:
        explicit Buffer(std::FILE* file) noexcept;
        void detach() noexcept;

    protected:
        std::streamsize xsputn(const char* data, std::streamsize size) override;
        int_type overflow(int_type value) override;
        int sync() override;

    private:
        std::FILE* file;
    };

    std::unique_ptr<std::FILE, FileCloser> file;
    Buffer buffer;
    std::ostream output;
};
} // namespace mimic
