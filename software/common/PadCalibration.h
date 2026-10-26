// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <array>
#include <cstdint>
#include <istream>
#include <string>

namespace mimic
{
struct SensorResponse
{
    float noiseMean = 1.0f;
    float noiseLimit = 0.0f;
    float threshold = 16.0f;
    float baseline = 0.0f;
};

struct VelocityResponse
{
    float minimum = 0.0f;
    float maximum = 2048.0f;
    float exponent = 1.0f;
    unsigned samples = 0;
};

// POD configuration copied into the acquisition worker before processing frames.
struct PadResponse
{
    bool calibrated = false;
    std::array<SensorResponse, 2> sensors{};
    std::array<VelocityResponse, 3> articulations{};
};

struct PadCalibration
{
    std::string pad;
    std::string session;
    std::string sourceHash;
    int head = -1;
    int rim = -1;
    int zones = 1;
    PadResponse response;
};

void validatePadResponse(const PadResponse& response, int zones);
PadCalibration readPadCalibration(std::istream& input);
std::string writePadCalibration(const PadCalibration& calibration);
unsigned calibratedVelocity(float peak, const VelocityResponse& response) noexcept;
} // namespace mimic
