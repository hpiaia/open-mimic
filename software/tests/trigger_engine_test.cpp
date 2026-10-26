// SPDX-License-Identifier: AGPL-3.0-only
#include "TriggerEngine.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

int main()
{
    try
    {
        unsigned calls = 0;
        mimic::TriggerEngine engine(
            [&calls](const mimic::DrumFeatures& features)
            {
                ++calls;
                mimic::DrumPrediction prediction;
                prediction.valid = true;
                prediction.classIndex = features[68] != 0 && features[32] > features[0] ? 2 : 1;
                prediction.probabilities.fill(0.0f);
                prediction.probabilities[prediction.classIndex] = 0.98f;
                return prediction;
            });
        engine.configure({{0, 1, 2}, {2, 3, 1}});
        std::array<std::uint16_t, 16> frame{};
        frame.fill(2);
        const auto feedQuiet = [&]
        {
            for (unsigned i = 0; i < 256; ++i)
            {
                engine.processFrame(0, frame);
            }
        };
        feedQuiet();
        if (calls != 0)
        {
            throw std::runtime_error("Silence invoked the classifier");
        }
        auto strike = [&](unsigned channel)
        {
            engine.beginBatch();
            for (unsigned i = 0; i < 64; ++i)
            {
                frame[channel] = static_cast<std::uint16_t>(
                    2 + std::lround(800.0 * std::exp(-static_cast<double>(i) / 8.0)));
                engine.processFrame(0, frame);
            }
            frame[channel] = 2;
        };
        strike(0);
        if (engine.eventCount() != 1 || engine.event(0).pad != 0 || engine.event(0).zone != 1)
        {
            throw std::runtime_error("Head strike did not produce exactly one event");
        }
        if (engine.event(0).velocity < 1 || engine.event(0).velocity > 127)
        {
            throw std::runtime_error("Velocity outside MIDI range");
        }
        feedQuiet();
        strike(1);
        if (engine.eventCount() != 1 || engine.event(0).zone != 2)
        {
            throw std::runtime_error("Rim strike was not classified");
        }
        feedQuiet();
        strike(3);
        if (engine.eventCount() != 0)
        {
            throw std::runtime_error("Inactive secondary sensor produced an event");
        }
        feedQuiet();
        strike(2);
        if (engine.eventCount() != 1 || engine.event(0).pad != 1 || engine.event(0).zone != 1)
        {
            throw std::runtime_error("Single-zone primary did not produce an event");
        }
        feedQuiet();
        engine.beginBatch();
        frame[0] = 4095;
        for (unsigned i = 0; i < 40; ++i)
        {
            engine.processFrame(0, frame);
        }
        if (engine.eventCount() != 0 || engine.clippedCount() == 0)
        {
            throw std::runtime_error("Clipped input was emitted");
        }
        engine.reset();
        engine.beginBatch();
        if (engine.eventCount() != 0)
        {
            throw std::runtime_error("Reset retained stale events");
        }
        frame.fill(2);
        feedQuiet();
        engine.beginBatch();
        for (unsigned i = 0; i < 64; ++i)
        {
            const auto value = static_cast<std::uint16_t>(
                2 + std::lround(700.0 * std::exp(-static_cast<double>(i) / 8.0)));
            frame[0] = value;
            frame[2] = value;
            engine.processFrame(0, frame);
        }
        if (engine.eventCount() != 2)
        {
            throw std::runtime_error("Simultaneous pads were collapsed into one event");
        }
        mimic::TriggerEngine shots(
            [](const mimic::DrumFeatures&)
            {
                mimic::DrumPrediction result;
                result.valid = true;
                result.classIndex = 3;
                result.probabilities = {0.0f, 0.0f, 0.0f, 1.0f};
                return result;
            });
        shots.configure({{0, 1, 2}});
        frame.fill(2);
        for (unsigned i = 0; i < 256; ++i)
        {
            shots.processFrame(0, frame);
        }
        shots.beginBatch();
        frame[0] = frame[1] = 502;
        for (unsigned i = 0; i < 32; ++i)
        {
            shots.processFrame(0, frame);
        }
        if (shots.eventCount() != 1 || shots.event(0).zone != 3)
        {
            throw std::runtime_error(
                "Four-class model rimshot was not preserved as a separate event");
        }
        std::cout << "Live event segmentation, head/rim, mono mask, retrigger and clipping checks "
                     "passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
