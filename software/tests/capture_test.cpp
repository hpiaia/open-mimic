// SPDX-License-Identifier: AGPL-3.0-only
#include "Capture.h"
#include "ExclusiveOutputFile.h"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace
{
void verifyLayout()
{
    const mimic::AdcLayout observed{8192, 4096, 32};
    observed.validate();

    try
    {
        mimic::AdcLayout{4096, 4096, 32}.validate();
    }
    catch (const std::runtime_error&)
    {
        return;
    }

    throw std::runtime_error("Invalid mapping accepted");
}

void verifyCaptureFormat()
{
    const mimic::AdcLayout layout{8192, 4096, 32};
    std::ostringstream output;
    mimic::writeCaptureHeader(output, {layout, layout}, 2000);
    mimic::writeCaptureRecord(output, 42, {{{7, 8}, {9, 10}}}, {{{1, 2}, {3, 4}}});

    const std::string expected{"OMADC002"
                               "\xd0\x07\x00\x00"
                               "\x00\x20\x00\x00\x00\x10\x00\x00\x20\x00\x00\x00"
                               "\x00\x20\x00\x00\x00\x10\x00\x00\x20\x00\x00\x00"
                               "\x2a\x00\x00\x00\x00\x00\x00\x00"
                               "\x07\x00\x00\x00\x08\x00\x00\x00"
                               "\x09\x00\x00\x00\x0a\x00\x00\x00"
                               "\x01\x02\x03\x04",
                               64};
    if (output.str() != expected)
    {
        throw std::runtime_error("Capture format differs from OMADC002");
    }
}

void verifyExclusiveCreation()
{
    auto pattern = (std::filesystem::temp_directory_path() / "mimic-capture-XXXXXX").string();
    if (::mkdtemp(pattern.data()) == nullptr)
    {
        throw std::system_error(errno, std::generic_category(), "Creating test directory");
    }

    struct TemporaryDirectory
    {
        std::filesystem::path path;

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } directory{pattern};

    const auto path = (directory.path / "capture.omadc").string();
    mimic::ExclusiveOutputFile first(path);
    first.stream() << "original capture";
    first.close();
    first.close();

    bool rejected = false;
    try
    {
        mimic::ExclusiveOutputFile duplicate(path);
    }
    catch (const std::system_error& error)
    {
        if (error.code() != std::errc::file_exists)
        {
            throw;
        }
        rejected = true;
    }

    std::ifstream input(path, std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>(input), {}};
    if (!rejected || contents != "original capture")
    {
        throw std::runtime_error("Existing capture was not preserved");
    }
}
} // namespace

int main()
{
    try
    {
        verifyLayout();
        verifyCaptureFormat();
        verifyExclusiveCreation();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
