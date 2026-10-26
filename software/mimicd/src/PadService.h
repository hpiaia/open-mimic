// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "PadControl.h"

#include <string>

namespace mimic
{
int runPadService(const std::string& dataDirectory = padDataDirectory);
int runPadCommand(int argc, char** argv);
} // namespace mimic
