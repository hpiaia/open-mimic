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

// Measured head/rim separation of one dual-zone pad. Ratios are rim peak over
// head peak inside the interpretation window. Rimshot levels are absolute ADC
// counts the *other* sensor must exceed, following the Mimic Pro procedure of
// placing the rimshot threshold just above the strongest cross-coupled energy.
struct ZoneRule
{
    bool calibrated = false;
    float headRatioCeiling = 0.5f;
    float rimRatioFloor = 1.5f;
    float rimshotHead = 4096.0f;
    float rimshotRim = 4096.0f;
    std::array<float, 2> decayMs{15.0f, 15.0f};
};

// POD configuration copied into the acquisition worker before processing frames.
struct PadResponse
{
    bool calibrated = false;
    std::array<SensorResponse, 2> sensors{};
    std::array<VelocityResponse, 3> articulations{};
    ZoneRule zone;
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

// Player-adjustable trigger settings, modelled on the Mimic Pro trigger screen.
// They sit on top of the measured response and never edit it. Negative rimshot
// thresholds select the calibrated value ("auto").
struct TriggerSettings
{
    enum ZoneMode : int
    {
        hybrid = 0,
        rule = 1,
        model = 2
    };

    float threshold = 4.0f;        // extra ADC counts above the measured onset threshold
    float maskMs = 8.0f;           // minimum time between two hits
    float retriggerCancel = 30.0f; // 0..100: required jump above the decaying envelope
    float decayMs = -1.0f;         // envelope time constant; negative selects calibrated
    std::array<float, 2> rimshotThreshold{-1.0f, -1.0f};
    float balance = 0.0f; // -100..100 shifts the head/rim boundary (log scale)
    int zoneMode = hybrid;
};

inline constexpr float triggerFramesPerMs = 11.25f;

void validatePadResponse(const PadResponse& response, int zones);
PadCalibration readPadCalibration(std::istream& input);
std::string writePadCalibration(const PadCalibration& calibration);
unsigned calibratedVelocity(float peak, const VelocityResponse& response) noexcept;

void validateTriggerSettings(const TriggerSettings& settings);
TriggerSettings readTriggerSettings(std::istream& input);
std::string writeTriggerSettings(const TriggerSettings& settings);
// Applies one "key value" edit; throws on unknown keys or out-of-range values.
void applyTriggerSetting(TriggerSettings& settings, const std::string& key, float value);
} // namespace mimic
