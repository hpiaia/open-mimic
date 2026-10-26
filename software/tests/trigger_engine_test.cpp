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
        mimic::PadResponse response;
        response.calibrated = true;
        response.sensors = {{{0.5f, 1.0f, 3.0f}, {0.5f, 1.0f, 3.0f}}};
        response.articulations = {
            {{8.0f, 800.0f, 1.0f, 20}, {12.0f, 500.0f, 1.0f, 20}, {30.0f, 1500.0f, 1.0f, 20}}};
        const auto calibratedEvent = [&](float headPeak, float rimPeak, unsigned zone)
        {
            mimic::TriggerEngine calibrated(
                [zone](const mimic::DrumFeatures&)
                {
                    mimic::DrumPrediction value;
                    value.valid = true;
                    value.classIndex = zone;
                    value.probabilities.fill(0.0f);
                    value.probabilities[zone] = 1.0f;
                    return value;
                });
            calibrated.configure({{0, 1, 2, response}});
            std::array<std::uint16_t, 16> sample{};
            sample.fill(2);
            for (unsigned j = 0; j < 256; ++j)
            {
                calibrated.processFrame(0, sample);
            }
            calibrated.beginBatch();
            sample[0] = static_cast<std::uint16_t>(2 + headPeak);
            sample[1] = static_cast<std::uint16_t>(2 + rimPeak);
            for (unsigned j = 0; j < 32; ++j)
            {
                calibrated.processFrame(0, sample);
            }
            if (calibrated.eventCount() != 1)
            {
                throw std::runtime_error("Calibrated sensitivity lost a valid hit");
            }
            return calibrated.event(0);
        };
        if (calibratedEvent(5, 0, 1).velocity != 1 || calibratedEvent(800, 20, 1).velocity != 127
            || calibratedEvent(50, 500, 2).velocity != 127
            || calibratedEvent(1500, 1000, 3).velocity != 127)
        {
            throw std::runtime_error("Per-articulation velocity or low-level onset differs");
        }
        // Dominant cross-coupled energy must not set a head hit's velocity.
        if (calibratedEvent(8, 500, 1).velocity != 1)
        {
            throw std::runtime_error("Head velocity incorrectly used the other sensor's peak");
        }
        // A strike during profile activation must not become the new quiet baseline.
        response.sensors[0].baseline = response.sensors[1].baseline = 2.0f;
        mimic::TriggerEngine activation(
            [](const mimic::DrumFeatures&)
            {
                mimic::DrumPrediction value;
                value.valid = true;
                value.classIndex = 1;
                value.probabilities = {0.0f, 1.0f, 0.0f, 0.0f};
                return value;
            });
        activation.configure({{0, 1, 2, response}});
        frame.fill(2);
        frame[0] = 700;
        for (unsigned i = 0; i < 64; ++i)
        {
            activation.processFrame(0, frame);
        }
        frame.fill(2);
        for (unsigned i = 0; i < 256; ++i)
        {
            activation.processFrame(0, frame);
        }
        activation.beginBatch();
        frame[0] = 802;
        for (unsigned i = 0; i < 32; ++i)
        {
            activation.processFrame(0, frame);
        }
        if (activation.eventCount() != 1 || activation.event(0).velocity != 127)
        {
            throw std::runtime_error("A startup strike contaminated the calibrated quiet baseline");
        }
        mimic::TriggerEngine bootstrap(
            [](const mimic::DrumFeatures&)
            {
                mimic::DrumPrediction result;
                result.valid = true;
                result.classIndex = 1;
                result.probabilities = {0.0f, 1.0f, 0.0f, 0.0f};
                return result;
            });
        bootstrap.configure({{0, 1, 2, {}}});
        frame.fill(2);
        frame[0] = 700;
        for (unsigned i = 0; i < 64; ++i)
        {
            bootstrap.processFrame(0, frame);
        }
        frame.fill(2);
        for (unsigned i = 0; i < 256; ++i)
        {
            bootstrap.processFrame(0, frame);
        }
        bootstrap.beginBatch();
        frame[0] = 802;
        for (unsigned i = 0; i < 32; ++i)
        {
            bootstrap.processFrame(0, frame);
        }
        if (bootstrap.eventCount() != 1)
        {
            throw std::runtime_error("Uncalibrated startup strike prevented later quiet recovery");
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
