// SPDX-License-Identifier: AGPL-3.0-only
#include "DrumModel.h"
#ifdef MIMIC_USE_LITERT
#include "LiteRtModel.h"
#endif

#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3
            && (argc != 4
                || (std::string(argv[3]) != "--single-zone" && std::string(argv[3]) != "--features"
                    && std::string(argv[3]) != "--batch-features")))
        {
            throw std::invalid_argument(
                "Usage: mimic-drum-classify MODEL WAVEFORM.txt [--single-zone]");
        }
#ifdef MIMIC_USE_LITERT
        mimic::LiteRtModel model(argv[1]);
        const auto zones = std::size_t{2};
        const auto classes = model.classCount();
#else
        std::ifstream input(argv[1]);
        const auto model = mimic::DrumModel::load(input);
        const auto zones = model.zoneCount();
        const auto classes = model.classCount();
#endif
        std::ifstream waveformFile(argv[2]);
        if (argc == 4 && std::string(argv[3]) == "--batch-features")
        {
            mimic::DrumFeatures row{};
            while (waveformFile >> row[0])
            {
                for (std::size_t index = 1; index < row.size(); ++index)
                {
                    if (!(waveformFile >> row[index]))
                    {
                        throw std::runtime_error("Incomplete feature row");
                    }
                }
                const auto prediction = model.predict(row);
                if (!prediction.valid)
                {
                    throw std::runtime_error("Invalid batch prediction");
                }
                std::cout << std::setprecision(9) << "{\"class\":" << prediction.classIndex
                          << ",\"probabilities\":[";
                for (std::size_t index = 0; index < classes; ++index)
                {
                    if (index != 0)
                    {
                        std::cout << ',';
                    }
                    std::cout << prediction.probabilities[index];
                }
                std::cout << "]}\n";
            }
            if (!waveformFile.eof())
            {
                throw std::runtime_error("Invalid feature input");
            }
            return 0;
        }

        const bool featuresInput = argc == 4 && std::string(argv[3]) == "--features";
        const bool singleZone = (argc == 4 && !featuresInput) || zones == 1;
#ifndef MIMIC_USE_LITERT
        if (argc == 4 && !model.supportsZoneMask())
        {
            throw std::runtime_error("Zone override requires a shared OMDRUM02 model");
        }
#endif
        mimic::DrumFeatures features{};
        mimic::DrumWaveform waveform{};
        if (featuresInput)
        {
            for (auto& feature : features)
            {
                if (!(waveformFile >> feature))
                {
                    throw std::runtime_error("Expected 69 features");
                }
            }
        }
        else
        {
            for (std::size_t index = 0; index < (singleZone ? 1U : zones); ++index)
            {
                for (auto& sample : waveform[index])
                {
                    if (!(waveformFile >> sample))
                    {
                        throw std::runtime_error(
                            "Expected 32 centered samples for each configured zone");
                    }
                }
            }
        }
        waveformFile >> std::ws;
        if (!waveformFile.eof())
        {
            throw std::runtime_error("Unexpected values after drum waveform");
        }
        if (!featuresInput)
        {
            features = mimic::drumFeatures(waveform, !singleZone);
        }
        const auto result = model.predict(features);
        if (!result.valid)
        {
            throw std::runtime_error("Invalid drum model prediction");
        }
        constexpr const char* labels[]{"background", "head", "rim", "rimshot"};
        std::cout << std::setprecision(9) << "{\"label\":\"" << labels[result.classIndex]
                  << "\",\"probabilities\":[";
        for (std::size_t index = 0; index < classes; ++index)
        {
            if (index != 0)
            {
                std::cout << ',';
            }
            std::cout << result.probabilities[index];
        }
        std::cout << "]}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "mimic-drum-classify: " << error.what() << '\n';
        return 1;
    }
}
