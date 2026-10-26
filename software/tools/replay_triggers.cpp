// SPDX-License-Identifier: AGPL-3.0-only
#include "DrumModel.h"
#include "PadCalibration.h"
#include "TriggerEngine.h"
#ifdef MIMIC_USE_LITERT
#include "LiteRtModel.h"
#endif

#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv)
{
    try
    {
        if (argc != 7)
        {
            throw std::invalid_argument(
                "Usage: mimic-trigger-replay MODEL|- HEAD RIM ZONES PROFILE|- FRAMES.txt");
        }
        const auto number = [](const char* text)
        {
            std::size_t used = 0;
            const auto value = std::stoi(text, &used);
            if (text[used] != '\0')
            {
                throw std::invalid_argument("Invalid integer argument");
            }
            return value;
        };
        mimic::TriggerPad pad{number(argv[2]), number(argv[3]), number(argv[4]), {}};
        if (pad.head < 0 || pad.head > 31 || (pad.zones == 2 && pad.rim < 0))
        {
            throw std::invalid_argument("Replay needs an assigned primary sensor");
        }
        if (std::string(argv[5]) != "-")
        {
            std::ifstream profile(argv[5]);
            const auto calibration = mimic::readPadCalibration(profile);
            if (calibration.head != pad.head || calibration.rim != pad.rim
                || calibration.zones != pad.zones)
            {
                throw std::invalid_argument("Response profile does not match replay sensors");
            }
            pad.response = calibration.response;
        }
        mimic::TriggerEngine::Predictor predict = [](const mimic::DrumFeatures&)
        { return mimic::DrumPrediction{}; };
#ifdef MIMIC_USE_LITERT
        std::unique_ptr<mimic::LiteRtModel> model;
        if (std::string(argv[1]) != "-")
        {
            model = std::make_unique<mimic::LiteRtModel>(argv[1]);
            predict = [&model](const mimic::DrumFeatures& features)
            { return model->predict(features); };
        }
#else
        if (std::string(argv[1]) != "-")
        {
            throw std::invalid_argument(
                "Build with LiteRT to replay a model; use - for features only");
        }
#endif
        std::uint64_t position = 0, candidates = 0, emitted = 0, frames = 0, resets = 0;
        mimic::TriggerEngine engine(
            [&](const mimic::DrumFeatures& features)
            {
                const auto result = predict(features);
                ++candidates;
                std::cout << "{\"kind\":\"candidate\",\"frame_end\":" << position
                          << ",\"valid\":" << (result.valid ? "true" : "false")
                          << ",\"class\":" << result.classIndex << ",\"probabilities\":[";
                for (std::size_t index = 0; index < result.probabilities.size(); ++index)
                {
                    std::cout << (index ? "," : "") << result.probabilities[index];
                }
                std::cout << "],\"features\":[";
                for (std::size_t index = 0; index < features.size(); ++index)
                {
                    std::cout << (index ? "," : "") << features[index];
                }
                std::cout << "]}\n";
                return result;
            });
        engine.configure({pad});
        std::ifstream input(argv[6]);
        if (!input)
        {
            throw std::runtime_error("Cannot read replay frames");
        }
        std::cout << std::setprecision(9);
        std::string line;
        bool havePosition = false;
        std::uint64_t previous = 0;
        while (std::getline(input, line))
        {
            std::istringstream row(line);
            std::int64_t index = 0;
            unsigned head = 0, rim = 0;
            if (!(row >> index))
            {
                throw std::runtime_error("Invalid replay row");
            }
            if (index == -1)
            {
                row >> std::ws;
                if (!row.eof())
                {
                    throw std::runtime_error("Gap row must contain only -1");
                }
                engine.reset();
                ++resets;
                continue;
            }
            if (index < 0 || !(row >> head >> rim) || head > 4095 || rim > 4095)
            {
                throw std::runtime_error("Expected frame index and two raw 12-bit sensor values");
            }
            row >> std::ws;
            if (!row.eof() || (havePosition && static_cast<std::uint64_t>(index) <= previous))
            {
                throw std::runtime_error("Replay frame positions must increase");
            }
            position = static_cast<std::uint64_t>(index);
            previous = position;
            havePosition = true;
            std::array<std::uint16_t, 16> frame{};
            frame[static_cast<std::size_t>(pad.head % 16)] = static_cast<std::uint16_t>(head);
            if (pad.zones == 2)
            {
                frame[static_cast<std::size_t>(pad.rim % 16)] = static_cast<std::uint16_t>(rim);
            }
            engine.beginBatch();
            engine.processFrame(static_cast<unsigned>(pad.head / 16), frame);
            ++frames;
            for (std::size_t event = 0; event < engine.eventCount(); ++event)
            {
                const auto& value = engine.event(event);
                ++emitted;
                std::cout << "{\"kind\":\"event\",\"frame_end\":" << position
                          << ",\"zone\":" << value.zone << ",\"velocity\":" << value.velocity
                          << ",\"probability\":" << value.probability << "}\n";
            }
        }
        if (!input.eof() || frames == 0)
        {
            throw std::runtime_error("Replay input read failed");
        }
        std::cout << "{\"kind\":\"summary\",\"frames\":" << frames
                  << ",\"candidates\":" << candidates << ",\"events\":" << emitted
                  << ",\"rejected\":" << engine.rejectedCount()
                  << ",\"clipped\":" << engine.clippedCount() << ",\"gap_resets\":" << resets
                  << "}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "mimic-trigger-replay: " << error.what() << '\n';
        return 1;
    }
}
