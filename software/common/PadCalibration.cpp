// SPDX-License-Identifier: AGPL-3.0-only
#include "PadCalibration.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace mimic
{
namespace
{
bool finiteRange(float value, float minimum, float maximum)
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool hexIdentity(const std::string& value, std::size_t size)
{
    return value.size() == size && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}

void validateZoneRule(const ZoneRule& rule)
{
    if (!rule.calibrated)
    {
        return;
    }
    if (!finiteRange(rule.headRatioCeiling, 0.0f, 64.0f)
        || !finiteRange(rule.rimRatioFloor, 0.0f, 64.0f)
        || rule.rimRatioFloor <= rule.headRatioCeiling
        || !finiteRange(rule.rimshotHead, 2.0f, 4096.0f)
        || !finiteRange(rule.rimshotRim, 2.0f, 4096.0f)
        || !finiteRange(rule.decayMs[0], 2.0f, 500.0f)
        || !finiteRange(rule.decayMs[1], 2.0f, 500.0f))
    {
        throw std::runtime_error("Invalid calibration zone rule");
    }
}
} // namespace

void validatePadResponse(const PadResponse& response, int zones)
{
    if (zones != 1 && zones != 2)
    {
        throw std::runtime_error("Invalid calibration sensor count");
    }
    if (!response.calibrated)
    {
        return;
    }
    for (int index = 0; index < zones; ++index)
    {
        const auto& sensor = response.sensors[static_cast<std::size_t>(index)];
        if (!finiteRange(sensor.noiseMean, 0.25f, 1024.0f)
            || !finiteRange(sensor.noiseLimit, 0.0f, 1023.0f)
            || !finiteRange(sensor.threshold, 2.0f, 1024.0f)
            || !finiteRange(sensor.baseline, 0.0f, 4089.0f)
            || sensor.threshold < sensor.noiseLimit + 1.0f)
        {
            throw std::runtime_error("Invalid calibration noise or threshold");
        }
    }
    const unsigned articulations = zones == 1 ? 1U : 3U;
    for (unsigned index = 0; index < articulations; ++index)
    {
        const auto& range = response.articulations[index];
        if (!finiteRange(range.minimum, 0.0f, 4088.0f) || !finiteRange(range.maximum, 8.0f, 4089.0f)
            || range.maximum - range.minimum < 8.0f || !finiteRange(range.exponent, 0.25f, 4.0f)
            || range.samples < 8 || range.samples > 1000000)
        {
            throw std::runtime_error("Invalid calibration velocity range or hit count");
        }
    }
    if (zones == 1 && response.zone.calibrated)
    {
        throw std::runtime_error("Single-zone pads carry no zone rule");
    }
    validateZoneRule(response.zone);
}

PadCalibration readPadCalibration(std::istream& input)
{
    input.imbue(std::locale::classic());
    PadCalibration value;
    std::string field;
    unsigned version = 0;
    if (!(input >> field >> version) || field != "OPEN_MIMIC_RESPONSE"
        || (version != 1 && version != 2))
    {
        throw std::runtime_error("Unsupported pad response profile");
    }
    const auto tag = [&](const char* expected)
    {
        if (!(input >> field) || field != expected)
        {
            throw std::runtime_error("Malformed pad response profile");
        }
    };
    tag("pad");
    input >> value.pad;
    tag("session");
    input >> value.session;
    tag("route");
    input >> value.head >> value.rim >> value.zones;
    tag("source");
    input >> value.sourceHash;
    if (value.pad.empty() || value.pad.size() > 16
        || value.pad.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789") != std::string::npos
        || !hexIdentity(value.session, 32) || !hexIdentity(value.sourceHash, 64) || value.head < 0
        || value.head > 31 || value.rim < 0 || value.rim > 31 || value.head == value.rim
        || value.head / 16 != value.rim / 16)
    {
        throw std::runtime_error("Invalid pad response identity or hardware route");
    }
    for (unsigned index = 0; index < 2; ++index)
    {
        unsigned sensor = 99;
        tag("sensor");
        input >> sensor;
        auto& response = value.response.sensors[index];
        input >> response.noiseMean >> response.noiseLimit >> response.threshold
            >> response.baseline;
        if (!input || sensor != index)
        {
            throw std::runtime_error("Malformed sensor response");
        }
    }
    for (unsigned index = 0; index < 3; ++index)
    {
        unsigned zone = 99;
        tag("zone");
        input >> zone;
        auto& range = value.response.articulations[index];
        input >> range.minimum >> range.maximum >> range.exponent >> range.samples;
        if (!input || zone != index + 1)
        {
            throw std::runtime_error("Malformed articulation response");
        }
    }
    if (version >= 2)
    {
        // "rule none" keeps the measured response without a head/rim separation.
        tag("rule");
        input >> field;
        if (field != "none")
        {
            auto& rule = value.response.zone;
            std::istringstream first(field);
            first.imbue(std::locale::classic());
            if (!(first >> rule.headRatioCeiling)
                || !(input >> rule.rimRatioFloor >> rule.rimshotHead >> rule.rimshotRim
                     >> rule.decayMs[0] >> rule.decayMs[1]))
            {
                throw std::runtime_error("Malformed zone rule");
            }
            rule.calibrated = true;
        }
    }
    input >> std::ws;
    if (!input.eof())
    {
        throw std::runtime_error("Unexpected data after pad response profile");
    }
    value.response.calibrated = true;
    validatePadResponse(value.response, value.zones);
    return value;
}

std::string writePadCalibration(const PadCalibration& value)
{
    validatePadResponse(value.response, value.zones);
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(9) << "OPEN_MIMIC_RESPONSE 2\npad " << value.pad << "\nsession "
           << value.session << "\nroute " << value.head << ' ' << value.rim << ' ' << value.zones
           << "\nsource " << value.sourceHash << '\n';
    for (unsigned index = 0; index < 2; ++index)
    {
        const auto& sensor = value.response.sensors[index];
        output << "sensor " << index << ' ' << sensor.noiseMean << ' ' << sensor.noiseLimit << ' '
               << sensor.threshold << ' ' << sensor.baseline << '\n';
    }
    for (unsigned index = 0; index < 3; ++index)
    {
        const auto& range = value.response.articulations[index];
        output << "zone " << index + 1 << ' ' << range.minimum << ' ' << range.maximum << ' '
               << range.exponent << ' ' << range.samples << '\n';
    }
    const auto& rule = value.response.zone;
    if (rule.calibrated)
    {
        output << "rule " << rule.headRatioCeiling << ' ' << rule.rimRatioFloor << ' '
               << rule.rimshotHead << ' ' << rule.rimshotRim << ' ' << rule.decayMs[0] << ' '
               << rule.decayMs[1] << '\n';
    }
    else
    {
        output << "rule none\n";
    }
    return output.str();
}

unsigned calibratedVelocity(float peak, const VelocityResponse& response) noexcept
{
    if (!std::isfinite(peak) || !std::isfinite(response.minimum) || !std::isfinite(response.maximum)
        || response.maximum <= response.minimum || !finiteRange(response.exponent, 0.25f, 4.0f))
    {
        return 1;
    }
    const auto normalized =
        std::clamp((peak - response.minimum) / (response.maximum - response.minimum), 0.0f, 1.0f);
    return static_cast<unsigned>(
        std::clamp(std::lround(1.0 + 126.0 * std::pow(normalized, response.exponent)), 1L, 127L));
}

namespace
{
struct SettingRange
{
    const char* key;
    float minimum;
    float maximum;
    bool allowAuto;
};

// Keys double as the on-disk and command vocabulary.
constexpr std::array<SettingRange, 8> settingRanges{{
    {"threshold", 0.0f, 1000.0f, false},
    {"mask", 2.0f, 50.0f, false},
    {"retrigger", 0.0f, 100.0f, false},
    {"decay", 2.0f, 500.0f, true},
    {"rimshot-head", 2.0f, 4095.0f, true},
    {"rimshot-rim", 2.0f, 4095.0f, true},
    {"balance", -100.0f, 100.0f, false},
    {"zone-mode", 0.0f, 2.0f, false},
}};

float& settingField(TriggerSettings& settings, const std::string& key, float& zoneMode)
{
    if (key == "threshold")
    {
        return settings.threshold;
    }
    if (key == "mask")
    {
        return settings.maskMs;
    }
    if (key == "retrigger")
    {
        return settings.retriggerCancel;
    }
    if (key == "decay")
    {
        return settings.decayMs;
    }
    if (key == "rimshot-head")
    {
        return settings.rimshotThreshold[0];
    }
    if (key == "rimshot-rim")
    {
        return settings.rimshotThreshold[1];
    }
    if (key == "balance")
    {
        return settings.balance;
    }
    if (key == "zone-mode")
    {
        zoneMode = static_cast<float>(settings.zoneMode);
        return zoneMode;
    }
    throw std::runtime_error("Unknown trigger setting: " + key);
}
} // namespace

void applyTriggerSetting(TriggerSettings& settings, const std::string& key, float value)
{
    const auto range = std::find_if(settingRanges.begin(),
                                    settingRanges.end(),
                                    [&key](const SettingRange& item) { return key == item.key; });
    if (range == settingRanges.end())
    {
        throw std::runtime_error("Unknown trigger setting: " + key);
    }
    const bool automatic = range->allowAuto && value < 0.0f;
    if (!std::isfinite(value) || (!automatic && (value < range->minimum || value > range->maximum)))
    {
        throw std::runtime_error("Trigger setting out of range: " + key);
    }
    if (key == "zone-mode")
    {
        settings.zoneMode = static_cast<int>(std::lround(value));
        return;
    }
    float unused = 0.0f;
    settingField(settings, key, unused) = automatic ? -1.0f : value;
}

void validateTriggerSettings(const TriggerSettings& settings)
{
    auto copy = settings;
    for (const auto& range : settingRanges)
    {
        float zoneMode = 0.0f;
        const auto value = settingField(copy, range.key, zoneMode);
        applyTriggerSetting(copy, range.key, value);
    }
}

TriggerSettings readTriggerSettings(std::istream& input)
{
    input.imbue(std::locale::classic());
    std::string field;
    unsigned version = 0;
    if (!(input >> field >> version) || field != "OPEN_MIMIC_TRIGGER" || version != 1)
    {
        throw std::runtime_error("Unsupported trigger settings file");
    }
    TriggerSettings settings;
    std::string key;
    while (input >> key)
    {
        float value = 0.0f;
        if (!(input >> value))
        {
            throw std::runtime_error("Malformed trigger setting: " + key);
        }
        applyTriggerSetting(settings, key, value);
    }
    return settings;
}

std::string writeTriggerSettings(const TriggerSettings& settings)
{
    validateTriggerSettings(settings);
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(9) << "OPEN_MIMIC_TRIGGER 1\n";
    auto copy = settings;
    for (const auto& range : settingRanges)
    {
        float zoneMode = 0.0f;
        output << range.key << ' ' << settingField(copy, range.key, zoneMode) << '\n';
    }
    return output.str();
}
} // namespace mimic
