// SPDX-License-Identifier: AGPL-3.0-only
#include "PadCalibration.h"

#include <cmath>
#include <sstream>
#include <stdexcept>

int main()
{
    mimic::PadCalibration profile;
    profile.pad = "tom1";
    profile.session = std::string(32, 'a');
    profile.sourceHash = std::string(64, 'b');
    profile.head = 26;
    profile.rim = 27;
    profile.zones = 2;
    profile.response.calibrated = true;
    for (auto& sensor : profile.response.sensors)
    {
        sensor = {0.5f, 2.0f, 4.0f};
    }
    profile.response.articulations = {
        {{8.0f, 800.0f, 1.0f, 30}, {12.0f, 500.0f, 1.0f, 25}, {30.0f, 1500.0f, 1.0f, 40}}};
    const auto serialized = mimic::writePadCalibration(profile);
    std::istringstream input(serialized);
    const auto loaded = mimic::readPadCalibration(input);
    if (loaded.head != 26 || loaded.session != profile.session
        || loaded.response.articulations[1].samples != 25)
    {
        throw std::runtime_error("Calibration profile round trip differs");
    }
    const auto& head = loaded.response.articulations[0];
    if (mimic::calibratedVelocity(8.0f, head) != 1 || mimic::calibratedVelocity(800.0f, head) != 127
        || mimic::calibratedVelocity(4000.0f, head) != 127
        || mimic::calibratedVelocity(0.0f, head) != 1)
    {
        throw std::runtime_error("Calibrated velocity endpoints differ");
    }
    unsigned previous = 0;
    for (unsigned peak = 0; peak < 4096; ++peak)
    {
        const auto velocity = mimic::calibratedVelocity(static_cast<float>(peak), head);
        if (velocity < previous || velocity < 1 || velocity > 127)
        {
            throw std::runtime_error("Velocity curve is not bounded and monotonic");
        }
        previous = velocity;
    }
    for (const auto& bad : {serialized + "extra",
                            serialized.substr(0, serialized.size() / 2),
                            std::string("OPEN_MIMIC_RESPONSE 99\n")})
    {
        bool rejected = false;
        try
        {
            std::istringstream stream(bad);
            (void)mimic::readPadCalibration(stream);
        }
        catch (...)
        {
            rejected = true;
        }
        if (!rejected)
        {
            throw std::runtime_error("Malformed calibration was accepted");
        }
    }
    profile.response.articulations[1].maximum = profile.response.articulations[1].minimum;
    bool rejected = false;
    try
    {
        mimic::validatePadResponse(profile.response, 2);
    }
    catch (...)
    {
        rejected = true;
    }
    if (!rejected)
    {
        throw std::runtime_error("Degenerate velocity range was accepted");
    }
}
