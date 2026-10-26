// SPDX-License-Identifier: AGPL-3.0-only
#include "AuxInputSettings.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

float gainAt(float decibels)
{
    return std::pow(10.0f, decibels / 20.0f);
}
} // namespace

int main()
{
    try
    {
        mimic::AuxInputSettings settings;
        settings.gain = gainAt(-6.0f);
        require(!settings.followHeadphones, "Aux must be independent by default");
        require(mimic::auxAttenuationCode(mimic::effectiveAuxGain(settings, 0.0f)) == 12,
                "Independent Aux must remain at -6 dB when headphones are muted");
        require(mimic::effectiveAuxGain(settings, std::numeric_limits<float>::quiet_NaN())
                    == settings.gain,
                "Independent Aux must not depend on invalid headphone gain");
        settings.followHeadphones = true;
        require(mimic::auxAttenuationCode(mimic::effectiveAuxGain(settings, gainAt(-10.0f))) == 32,
                "Linked -6 dB Aux and -10 dB headphones must produce -16 dB");
        require(mimic::auxAttenuationCode(mimic::effectiveAuxGain(settings, 0.0f)) == 127,
                "Linked headphone mute must mute Aux");
        require(mimic::auxAttenuationCode(1.0f) == 0, "Unity gain must have no attenuation");
        require(mimic::auxAttenuationCode(gainAt(-48.0f)) == 64,
                "Attenuator coarse range boundary must be decoded correctly");
        require(mimic::auxAttenuationCode(gainAt(-76.0f)) == 78,
                "The last finite attenuation must remain audible");
        require(mimic::auxAttenuationCode(gainAt(-80.0f)) == 127,
                "Gains below the attenuation range must mute");
        unsigned previous = 0;
        for (int step = 0; step <= 800; ++step)
        {
            const auto code = mimic::auxAttenuationCode(gainAt(-static_cast<float>(step) / 10.0f));
            require(code >= previous, "Attenuation must be monotonic through range changes");
            previous = code;
        }
        std::cout << "Aux independent/linked gain and attenuation checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
