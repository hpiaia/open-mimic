// SPDX-License-Identifier: AGPL-3.0-only
#include "DrumModel.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace
{
void require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

std::string fixture(float scale = 1.0f, std::size_t zones = 2, bool shared = false)
{
    std::ostringstream text;
    text << (shared ? "OMDRUM02 32 69 " : "OMDRUM01 32 68 ") << zones + 1 << ' ' << zones
         << "\nmean ";
    for (std::size_t index = 0;
         index < (shared ? mimic::drumFeatureCount : mimic::drumFeatureCount - 1);
         ++index)
    {
        text << "0 ";
    }
    text << "\nscale ";
    for (std::size_t index = 0;
         index < (shared ? mimic::drumFeatureCount : mimic::drumFeatureCount - 1);
         ++index)
    {
        text << scale << ' ';
    }
    text << "\nweights ";
    for (std::size_t label = 0; label < zones + 1; ++label)
    {
        for (std::size_t index = 0;
             index < (shared ? mimic::drumFeatureCount : mimic::drumFeatureCount - 1);
             ++index)
        {
            const bool active = (label == 1 && index == 0) || (label == 2 && index == 32);
            text << (active ? 5 : 0) << ' ';
        }
    }
    text << (zones == 1 ? "\nbias 1 0\n" : "\nbias 1 0 0\n");
    return text.str();
}

void rejects(const std::string& text)
{
    std::istringstream input(text);
    try
    {
        (void)mimic::DrumModel::load(input);
    }
    catch (const std::runtime_error&)
    {
        return;
    }
    throw std::runtime_error("Malformed model was accepted");
}
} // namespace

int main()
{
    try
    {
        std::istringstream input(fixture());
        const auto model = mimic::DrumModel::load(input);
        mimic::DrumWaveform waveform{};
        const auto quiet = model.predict(mimic::drumFeatures(waveform));
        require(quiet.valid && quiet.classIndex == 0, "Quiet fixture did not select background");
        waveform[0][0] = 200;
        waveform[1][0] = 50;
        const auto features = mimic::drumFeatures(waveform);
        require(features[0] == 1.0f && features[32] == 0.25f,
                "Waveform channel balance was not preserved");
        require(features[66] == 1.0f / 32.0f && features[67] == 0.25f / 32.0f,
                "Waveform magnitude features differ");
        const auto head = model.predict(features);
        const auto denominator = std::exp(1.0) + std::exp(5.0) + std::exp(1.25);
        require(head.valid && head.classIndex == 1
                    && std::abs(head.probabilities[1] - std::exp(5.0) / denominator) < 1.0e-6,
                "Golden softmax prediction differs");
        std::swap(waveform[0], waveform[1]);
        require(model.predict(mimic::drumFeatures(waveform)).classIndex == 2,
                "Swapping the dominant channel did not select rim");
        auto invalid = features;
        invalid[7] = std::numeric_limits<float>::quiet_NaN();
        require(!model.predict(invalid).valid, "Non-finite features were accepted");
        bool rejected = false;
        waveform[0][0] = 4096;
        try
        {
            (void)mimic::drumFeatures(waveform);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "Out-of-range ADC waveform was accepted");
        std::istringstream singleInput(fixture(1.0f, 1));
        const auto single = mimic::DrumModel::load(singleInput);
        waveform = {};
        waveform[0][0] = 200;
        const auto singlePrediction = single.predict(mimic::drumFeatures(waveform));
        require(single.zoneCount() == 1 && single.classCount() == 2 && singlePrediction.valid
                    && singlePrediction.classIndex == 1
                    && singlePrediction.probabilities[2] == 0.0f,
                "Single-zone model did not disable the absent rim class");
        std::istringstream sharedInput(fixture(1.0f, 2, true));
        const auto shared = mimic::DrumModel::load(sharedInput);
        waveform = {};
        waveform[1][0] = 200;
        require(shared.supportsZoneMask()
                    && shared.predict(mimic::drumFeatures(waveform)).classIndex == 2,
                "Shared model did not recognize an active rim");
        auto maskedFeatures = mimic::drumFeatures(waveform, false);
        const auto maskedPrediction = shared.predict(maskedFeatures);
        require(maskedFeatures[32] == 0.0f && maskedFeatures[68] == 0.0f && maskedPrediction.valid
                    && maskedPrediction.classIndex == 0
                    && maskedPrediction.probabilities[2] == 0.0f,
                "Inactive secondary sensor participated in shared inference");
        maskedFeatures[68] = 0.5f;
        require(!shared.predict(maskedFeatures).valid, "Invalid sensor mask was accepted");
        rejects("OMDRUM01 64 68 3\n");
        rejects(fixture(0.0f));
        rejects(fixture().substr(0, fixture().size() - 8));
        rejects(fixture() + "unexpected\n");
        std::cout << "Drum waveform features, golden inference and malformed models passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
