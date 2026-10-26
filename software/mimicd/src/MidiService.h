// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>

namespace mimic
{
int runMidiService(const std::string& dinDevice = "/dev/ttyS9");
int runMidiCommand(int argc, char** argv);
} // namespace mimic
