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
}

PadCalibration readPadCalibration(std::istream& input)
{
    input.imbue(std::locale::classic());
    PadCalibration value;
    std::string field;
    unsigned version = 0;
    if (!(input >> field >> version) || field != "OPEN_MIMIC_RESPONSE" || version != 1)
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
    output << std::setprecision(9) << "OPEN_MIMIC_RESPONSE 1\npad " << value.pad << "\nsession "
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
} // namespace mimic
