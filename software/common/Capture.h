// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "Adc.h"

#include <array>
#include <ostream>

namespace mimic
{
void writeCaptureHeader(std::ostream& output,
                        const std::array<AdcLayout, 2>& layouts,
                        std::uint32_t pollMicroseconds);
void writeCaptureRecord(std::ostream& output,
                        std::uint64_t elapsedNanoseconds,
                        const std::array<std::pair<std::uint32_t, std::uint32_t>, 2>& cursors,
                        const std::array<std::vector<std::uint8_t>, 2>& buffers);
void capture(const std::string& path, unsigned seconds);
} // namespace mimic
