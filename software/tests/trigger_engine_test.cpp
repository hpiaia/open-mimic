// SPDX-License-Identifier: AGPL-3.0-only
#include "TriggerEngine.h"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
using Frame = std::array<std::uint16_t, 16>;

void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

mimic::TriggerEngine::Predictor constant(unsigned zone, float probability)
{
    return [zone, probability](const mimic::DrumFeatures&)
    {
        mimic::DrumPrediction value;
        value.valid = true;
        value.classIndex = zone;
        value.probabilities.fill(0.0f);
        value.probabilities[zone] = probability;
        return value;
    };
}

mimic::TriggerPad dualPad(int head, int rim)
{
    mimic::TriggerPad pad;
    pad.head = head;
    pad.rim = rim;
    pad.zones = 2;
    return pad;
}

mimic::PadResponse calibratedResponse()
{
    mimic::PadResponse response;
    response.calibrated = true;
    response.sensors = {{{0.5f, 1.0f, 3.0f, 2.0f}, {0.5f, 1.0f, 3.0f, 2.0f}}};
    response.articulations = {
        {{8.0f, 800.0f, 1.0f, 20}, {12.0f, 500.0f, 1.0f, 20}, {30.0f, 1500.0f, 1.0f, 20}}};
    response.zone.calibrated = true;
    response.zone.headRatioCeiling = 0.3f;
    response.zone.rimRatioFloor = 1.3f;
    response.zone.rimshotHead = 600.0f;
    response.zone.rimshotRim = 150.0f;
    response.zone.decayMs = {12.0f, 12.0f};
    return response;
}

struct Kit
{
    explicit Kit(mimic::TriggerEngine::Predictor predictor) : engine(std::move(predictor))
    {
        frame.fill(2);
    }

    void quiet(unsigned frames = 256)
    {
        frame.fill(2);
        for (unsigned i = 0; i < frames; ++i)
        {
            engine.processFrame(0, frame);
        }
    }

    // Decaying strike with the given sensor peaks; returns events produced.
    std::size_t strike(float headPeak, float rimPeak, unsigned frames = 64, double tau = 8.0)
    {
        engine.beginBatch();
        std::size_t produced = 0;
        for (unsigned i = 0; i < frames; ++i)
        {
            const auto decay = std::exp(-static_cast<double>(i) / tau);
            frame[0] = static_cast<std::uint16_t>(2 + std::lround(headPeak * decay));
            frame[1] = static_cast<std::uint16_t>(2 + std::lround(rimPeak * decay));
            engine.processFrame(0, frame);
            produced += engine.eventCount();
            engine.beginBatch();
        }
        frame[0] = frame[1] = 2;
        return produced;
    }

    mimic::TriggerEngine engine;
    Frame frame{};
    mimic::TriggerEvent last{};
};

void stageOneChecks()
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
    mimic::TriggerPad mono;
    mono.head = 2;
    mono.rim = 3;
    mono.zones = 1;
    engine.configure({dualPad(0, 1), mono});
    Frame frame{};
    frame.fill(2);
    const auto feedQuiet = [&]
    {
        frame.fill(2);
        for (unsigned i = 0; i < 256; ++i)
        {
            engine.processFrame(0, frame);
        }
    };
    feedQuiet();
    require(calls == 0, "Silence invoked the classifier");
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
    require(engine.eventCount() == 1 && engine.event(0).pad == 0 && engine.event(0).zone == 1,
            "Head strike did not produce exactly one event");
    require(engine.event(0).velocity >= 1 && engine.event(0).velocity <= 127,
            "Velocity outside MIDI range");
    require(!engine.event(0).modelDecided && calls == 0,
            "An unambiguous head strike consulted the classifier");
    feedQuiet();
    strike(1);
    require(engine.eventCount() == 1 && engine.event(0).zone == 2, "Rim strike was not classified");
    feedQuiet();
    strike(3);
    require(engine.eventCount() == 0, "Inactive secondary sensor produced an event");
    feedQuiet();
    strike(2);
    require(engine.eventCount() == 1 && engine.event(0).pad == 1 && engine.event(0).zone == 1,
            "Single-zone primary did not produce an event");
    feedQuiet();
    engine.beginBatch();
    frame[0] = 4095;
    for (unsigned i = 0; i < 40; ++i)
    {
        engine.processFrame(0, frame);
    }
    require(engine.eventCount() == 0 && engine.clippedCount() != 0, "Clipped input was emitted");
    engine.reset();
    engine.beginBatch();
    require(engine.eventCount() == 0, "Reset retained stale events");
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
    require(engine.eventCount() == 2, "Simultaneous pads were collapsed into one event");
    std::cout << "stage one: segmentation, zones, mono mask and clipping passed\n";
}

void retriggerChecks()
{
    // Default settings: 8 ms mask, retrigger cancel 30, 15 ms envelope.
    Kit kit(constant(1, 0.99f));
    kit.engine.configure({dualPad(0, 1)});
    kit.quiet();
    // One long decaying strike (about 23 ms above threshold) is exactly one hit.
    require(kit.strike(900.0f, 0.0f, 256, 40.0) == 1, "A slow decay retriggered");
    kit.quiet();
    // Two equal strikes 12 ms apart (135 frames) are both detected without quiet between them.
    kit.engine.beginBatch();
    std::size_t produced = 0;
    for (unsigned i = 0; i < 400; ++i)
    {
        const auto first = 900.0 * std::exp(-static_cast<double>(i) / 60.0);
        const auto second = i >= 135 ? 900.0 * std::exp(-static_cast<double>(i - 135) / 60.0) : 0.0;
        kit.frame[0] = static_cast<std::uint16_t>(2 + std::lround(first + second));
        kit.engine.processFrame(0, kit.frame);
        produced += kit.engine.eventCount();
        kit.engine.beginBatch();
    }
    require(produced == 2,
            "A second hit during the first hit's decay was lost: " + std::to_string(produced));
    kit.quiet();
    // Hits 4 ms apart violate the 8 ms mask: only the first is emitted.
    produced = 0;
    for (unsigned i = 0; i < 300; ++i)
    {
        const auto first = 900.0 * std::exp(-static_cast<double>(i) / 20.0);
        const auto second = i >= 45 ? 900.0 * std::exp(-static_cast<double>(i - 45) / 20.0) : 0.0;
        kit.frame[0] = static_cast<std::uint16_t>(2 + std::lround(first + second));
        kit.engine.processFrame(0, kit.frame);
        produced += kit.engine.eventCount();
        kit.engine.beginBatch();
    }
    require(produced == 1, "Mask time did not suppress a hit inside 8 ms");
    kit.quiet();
    // A ghost note well inside the decay must exceed the envelope by the retrigger factor.
    produced = 0;
    for (unsigned i = 0; i < 600; ++i)
    {
        const auto first = 900.0 * std::exp(-static_cast<double>(i) / 100.0);
        const auto ghost = i >= 225 ? 40.0 * std::exp(-static_cast<double>(i - 225) / 30.0) : 0.0;
        kit.frame[0] = static_cast<std::uint16_t>(2 + std::lround(first + ghost));
        kit.engine.processFrame(0, kit.frame);
        produced += kit.engine.eventCount();
        kit.engine.beginBatch();
    }
    require(produced == 1, "A tiny bump riding a loud decay was treated as a new hit");
    // With a short mask and no retrigger cancel, a strong jump 3 ms later is a hit.
    auto fast = dualPad(0, 1);
    fast.settings.maskMs = 2.0f;
    fast.settings.retriggerCancel = 0.0f;
    kit.engine.configure({fast});
    kit.quiet();
    produced = 0;
    for (unsigned i = 0; i < 300; ++i)
    {
        const auto first = 300.0 * std::exp(-static_cast<double>(i) / 10.0);
        const auto second = i >= 36 ? 900.0 * std::exp(-static_cast<double>(i - 36) / 10.0) : 0.0;
        kit.frame[0] = static_cast<std::uint16_t>(2 + std::lround(first + second));
        kit.engine.processFrame(0, kit.frame);
        produced += kit.engine.eventCount();
        kit.engine.beginBatch();
    }
    require(produced == 2, "Short mask and retrigger cancel 0 still lost a flam");
    std::cout << "stage one: envelope retrigger cancel and mask time passed\n";
}

void zoneRuleChecks()
{
    // The classifier insists on "head"; the rule must still own unambiguous cases.
    Kit kit(constant(1, 1.0f));
    auto pad = dualPad(0, 1);
    pad.response = calibratedResponse();
    kit.engine.configure({pad});
    const auto hit = [&](float head, float rim)
    {
        kit.quiet();
        kit.engine.beginBatch();
        kit.frame[0] = static_cast<std::uint16_t>(2 + head);
        kit.frame[1] = static_cast<std::uint16_t>(2 + rim);
        for (unsigned j = 0; j < 32; ++j)
        {
            kit.engine.processFrame(0, kit.frame);
        }
        kit.frame[0] = kit.frame[1] = 2;
        require(kit.engine.eventCount() == 1, "Calibrated hit was lost");
        return kit.engine.event(0);
    };
    require(hit(500, 20).zone == 1 && hit(500, 20).ruleZone == 1, "Head-dominant hit is head");
    require(hit(500, 20).velocity == 79, "Head velocity did not follow the head response");
    require(hit(40, 400).zone == 2, "Rim-dominant hit is rim");
    require(hit(40, 400).velocity == 101, "Rim velocity did not follow the rim response");
    require(hit(1000, 200).zone == 3, "Strong rim coupling above the rimshot level is a rimshot");
    require(hit(900, 2000).zone == 3, "Strong head energy under a rim hit is a rimshot");
    require(hit(400, 300).ruleZone == 2 && hit(400, 300).modelDecided && hit(400, 300).zone == 1,
            "Comparable sensors fall in the ambiguous band and let the model decide");
    // Hybrid: the model only speaks near a boundary. 0.3 ceiling; ratio 0.29 is near.
    const auto near = hit(1000, 290);
    require(near.modelDecided && near.zone == 1, "Boundary case did not consult the classifier");
    const auto far = hit(1000, 100);
    require(!far.modelDecided && far.zone == 1, "Clear head hit consulted the classifier");
    // Rule mode ignores the classifier completely.
    pad.settings.zoneMode = mimic::TriggerSettings::rule;
    kit.engine.configure({pad});
    require(!hit(400, 300).modelDecided && hit(400, 300).zone == 2, "Rule mode used the model");
    require(hit(364, 39).zone == 1, "A hard head hit with normal coupling became a rimshot");
    // Model mode follows a confident classifier but never drops the hit.
    pad.settings.zoneMode = mimic::TriggerSettings::model;
    kit.engine.configure({pad});
    require(hit(40, 400).zone == 1 && hit(40, 400).modelDecided,
            "Model mode did not follow the confident classifier");
    Kit unsure(constant(0, 1.0f));
    unsure.engine.configure({pad});
    const auto kept = [&](float head, float rim)
    {
        unsure.quiet();
        unsure.engine.beginBatch();
        unsure.frame[0] = static_cast<std::uint16_t>(2 + head);
        unsure.frame[1] = static_cast<std::uint16_t>(2 + rim);
        for (unsigned j = 0; j < 32; ++j)
        {
            unsure.engine.processFrame(0, unsure.frame);
        }
        unsure.frame[0] = unsure.frame[1] = 2;
        require(unsure.engine.eventCount() == 1, "A background verdict dropped a detected hit");
        return unsure.engine.event(0);
    };
    require(kept(40, 400).zone == 2 && !kept(40, 400).modelDecided,
            "A rejected classifier verdict did not fall back to the rule");
    // Balance shifts the ratio boundary; rimshot overrides replace calibrated levels.
    pad.settings.zoneMode = mimic::TriggerSettings::rule;
    pad.settings.balance = 100.0f;
    kit.engine.configure({pad});
    require(kit.engine.effectiveRule(0).headRatioCeiling > 0.59f
                && kit.engine.effectiveRule(0).headRatioCeiling < 0.61f,
            "Balance did not double the head ceiling");
    require(hit(400, 140).zone == 1, "Balance toward head did not reclassify a band ratio");
    pad.settings.balance = 0.0f;
    pad.settings.rimshotThreshold[1] = 3000.0f;
    kit.engine.configure({pad});
    require(hit(1000, 200).zone == 1, "A raised rim rimshot threshold still produced a rimshot");
    std::cout << "stage two: calibrated zone rule, hybrid ambiguity and modes passed\n";
}

void legacyAndSettingsChecks()
{
    Frame frame{};
    auto response = calibratedResponse();
    response.sensors[0].baseline = response.sensors[1].baseline = 2.0f;
    mimic::TriggerEngine activation(constant(1, 1.0f));
    auto pad = dualPad(0, 1);
    pad.response = response;
    activation.configure({pad});
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
    require(activation.eventCount() == 1 && activation.event(0).velocity == 127,
            "A startup strike contaminated the calibrated quiet baseline");
    // A pad-specific predictor overrides the shared one; class gates are per pad.
    mimic::TriggerEngine mixed(constant(1, 0.9f));
    auto own = dualPad(0, 1);
    own.predictor = constant(2, 0.6f);
    own.gate = {0.0f, 0.65f, 0.55f, 0.65f};
    own.settings.zoneMode = mimic::TriggerSettings::model;
    auto shared = dualPad(2, 3);
    shared.gate = {0.0f, 0.95f, 0.65f, 0.65f};
    shared.settings.zoneMode = mimic::TriggerSettings::model;
    mixed.configure({own, shared});
    frame.fill(2);
    for (unsigned i = 0; i < 256; ++i)
    {
        mixed.processFrame(0, frame);
    }
    mixed.beginBatch();
    frame[0] = frame[2] = 702;
    for (unsigned i = 0; i < 32; ++i)
    {
        mixed.processFrame(0, frame);
    }
    require(mixed.eventCount() == 2, "A gated-out classifier dropped a detected hit");
    require(mixed.event(0).pad == 0 && mixed.event(0).zone == 2 && mixed.event(0).modelDecided,
            "Per-pad predictor or per-class gate was not applied");
    require(mixed.event(1).pad == 1 && mixed.event(1).zone == 1 && !mixed.event(1).modelDecided,
            "Shared pad with a stricter gate did not fall back to the rule");
    auto invalid = dualPad(0, 1);
    invalid.gate = {0.0f, 1.5f, 0.65f, 0.65f};
    bool rejectedGate = false;
    try
    {
        mixed.configure({invalid});
    }
    catch (const std::invalid_argument&)
    {
        rejectedGate = true;
    }
    require(rejectedGate, "An out-of-range class gate was accepted");
    auto badSettings = dualPad(0, 1);
    badSettings.settings.maskMs = 500.0f;
    bool rejectedSettings = false;
    try
    {
        mixed.configure({badSettings});
    }
    catch (const std::exception&)
    {
        rejectedSettings = true;
    }
    require(rejectedSettings, "An out-of-range mask time was accepted");
    mimic::TriggerEngine bootstrap(constant(1, 1.0f));
    bootstrap.configure({dualPad(0, 1)});
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
    require(bootstrap.eventCount() == 1,
            "Uncalibrated startup strike prevented later quiet recovery");
    // Trigger settings serialize through their command vocabulary.
    mimic::TriggerSettings settings;
    mimic::applyTriggerSetting(settings, "mask", 12.0f);
    mimic::applyTriggerSetting(settings, "rimshot-rim", 180.0f);
    mimic::applyTriggerSetting(settings, "zone-mode", 1.0f);
    mimic::applyTriggerSetting(settings, "decay", -5.0f);
    const auto text = mimic::writeTriggerSettings(settings);
    std::istringstream input(text);
    const auto loaded = mimic::readTriggerSettings(input);
    require(loaded.maskMs == 12.0f && loaded.rimshotThreshold[1] == 180.0f
                && loaded.rimshotThreshold[0] == -1.0f && loaded.zoneMode == 1
                && loaded.decayMs == -1.0f,
            "Trigger settings round trip differs");
    bool rejectedKey = false;
    try
    {
        mimic::applyTriggerSetting(settings, "sensitivity", 1.0f);
    }
    catch (const std::exception&)
    {
        rejectedKey = true;
    }
    require(rejectedKey, "An unknown trigger setting was accepted");
    std::cout << "legacy startup, per-pad predictors and trigger settings passed\n";
}
} // namespace

int main()
{
    try
    {
        stageOneChecks();
        retriggerChecks();
        zoneRuleChecks();
        legacyAndSettingsChecks();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "trigger_engine_test: " << error.what() << '\n';
        return 1;
    }
}
