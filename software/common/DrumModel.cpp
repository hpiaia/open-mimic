// SPDX-License-Identifier: AGPL-3.0-only
#include "DrumModel.h"

#include <algorithm>
#include <cmath>
#include <locale>
#include <stdexcept>
#include <string>

namespace mimic
{
DrumFeatures drumFeatures(const DrumWaveform& waveform, bool secondaryPresent)
{
    std::array<float, 2> peaks{};
    std::array<float, 2> magnitudes{};
    for (std::size_t channel = 0; channel < waveform.size(); ++channel)
    {
        if (channel == 1 && !secondaryPresent)
        {
            continue;
        }
        for (const auto sample : waveform[channel])
        {
            if (!std::isfinite(sample) || std::abs(sample) > 4095.0f)
            {
                throw std::invalid_argument("Expected finite, centered 12-bit ADC samples");
            }
            peaks[channel] = std::max(peaks[channel], std::abs(sample));
            magnitudes[channel] += std::abs(sample);
        }
    }
    const auto peak = std::max({1.0f, peaks[0], peaks[1]});
    DrumFeatures result{};
    for (std::size_t channel = 0; channel < waveform.size(); ++channel)
    {
        if (channel == 1 && !secondaryPresent)
        {
            continue;
        }
        for (std::size_t frame = 0; frame < drumWindowFrames; ++frame)
        {
            result[channel * drumWindowFrames + frame] = waveform[channel][frame] / peak;
        }
        result[64 + channel] = std::log1p(peaks[channel]) / std::log(4096.0f);
        result[66 + channel] = magnitudes[channel] / (peak * 32.0f);
    }
    result[68] = secondaryPresent ? 1.0f : 0.0f;
    return result;
}

DrumModel DrumModel::load(std::istream& input)
{
    input.imbue(std::locale::classic());
    std::string magic;
    std::size_t frames = 0;
    std::size_t features = 0;
    std::size_t classes = 0;
    std::size_t zones = 0;
    if (!(input >> magic >> frames >> features >> classes >> zones)
        || (magic != "OMDRUM01" && magic != "OMDRUM02") || frames != drumWindowFrames
        || features != (magic == "OMDRUM02" ? drumFeatureCount : drumFeatureCount - 1)
        || (zones != 1 && zones != 2) || classes != zones + 1
        || (magic == "OMDRUM02" && zones != 2))
    {
        throw std::runtime_error("Unsupported drum waveform model header");
    }
    const auto tag = [&input](const char* expected)
    {
        std::string actual;
        if (!(input >> actual) || actual != expected)
        {
            throw std::runtime_error("Invalid drum waveform model section");
        }
    };
    const auto value = [&input]()
    {
        float result = 0.0f;
        if (!(input >> result) || !std::isfinite(result) || std::abs(result) > 1000000.0f)
        {
            throw std::runtime_error("Invalid drum waveform model coefficient");
        }
        return result;
    };
    DrumModel result;
    result.masked = magic == "OMDRUM02";
    result.scales.fill(1.0f);
    result.zones = zones;
    result.activeClasses = classes;
    tag("mean");
    for (std::size_t index = 0; index < features; ++index)
    {
        result.means[index] = value();
    }
    tag("scale");
    for (std::size_t index = 0; index < features; ++index)
    {
        auto& element = result.scales[index];
        element = value();
        if (element <= 0.0f)
        {
            throw std::runtime_error("Drum waveform feature scales must be positive");
        }
    }
    tag("weights");
    for (std::size_t index = 0; index < classes; ++index)
    {
        for (std::size_t feature = 0; feature < features; ++feature)
        {
            result.weights[index][feature] = value();
        }
    }
    tag("bias");
    for (std::size_t index = 0; index < classes; ++index)
    {
        result.biases[index] = value();
    }
    input >> std::ws;
    if (!input.eof())
    {
        throw std::runtime_error("Unexpected data after drum waveform model");
    }
    return result;
}

DrumPrediction DrumModel::predict(const DrumFeatures& features) const noexcept
{
    if (masked && features[68] != 0.0f && features[68] != 1.0f)
    {
        return {};
    }
    const auto enabledClasses = masked && features[68] == 0.0f ? std::size_t{2} : activeClasses;
    std::array<double, drumClassCount> scores{};
    for (std::size_t channel = 0; channel < enabledClasses; ++channel)
    {
        scores[channel] = biases[channel];
        for (std::size_t feature = 0; feature < drumFeatureCount; ++feature)
        {
            if (!std::isfinite(features[feature]))
            {
                return {};
            }
            scores[channel] += (static_cast<double>(features[feature]) - means[feature])
                               * scales[feature] * weights[channel][feature];
        }
        if (!std::isfinite(scores[channel]))
        {
            return {};
        }
    }
    const auto maximum = *std::max_element(
        scores.begin(), scores.begin() + static_cast<std::ptrdiff_t>(enabledClasses));
    double total = 0.0;
    for (std::size_t index = 0; index < enabledClasses; ++index)
    {
        scores[index] = std::exp(scores[index] - maximum);
        total += scores[index];
    }
    DrumPrediction prediction;
    prediction.probabilities.fill(0.0f);
    for (std::size_t channel = 0; channel < enabledClasses; ++channel)
    {
        prediction.probabilities[channel] = static_cast<float>(scores[channel] / total);
    }
    prediction.classIndex = static_cast<std::size_t>(std::distance(
        prediction.probabilities.begin(),
        std::max_element(prediction.probabilities.begin(), prediction.probabilities.end())));
    prediction.valid = true;
    return prediction;
}
} // namespace mimic
