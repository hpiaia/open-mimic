// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "TouchChoice.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

// Per-pad trigger settings in the style of the Mimic Pro trigger screen:
// threshold, mask time, retrigger cancel, rimshot thresholds and head/rim
// balance over the measured calibration, plus the zone decision mode.
class TriggerSettingsPanel final : public juce::Component, private juce::Timer
{
public:
    TriggerSettingsPanel();
    ~TriggerSettingsPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void show();
    std::function<void()> onClose;

private:
    struct Pad
    {
        juce::String id, name;
        int zones = 1, head = -1;
        float threshold = 0.0f, maskMs = 8.0f, retriggerCancel = 30.0f, balance = 0.0f;
        float rimshotHead = -1.0f, rimshotRim = -1.0f;
        int zoneMode = 0;
        bool measured = false;
        float measuredRimshotHead = 0.0f, measuredRimshotRim = 0.0f;
        float headCeiling = 0.0f, rimFloor = 0.0f;
        float effectiveRimshotHead = 0.0f, effectiveRimshotRim = 0.0f;
        float lastHeadPeak = 0.0f, lastRimPeak = 0.0f;
        int lastZone = 0, lastRuleZone = 0, lastVelocity = 0;
        bool lastModelDecided = false;
        juce::int64 hits = 0;
        bool modelActive = false;
    };

    struct Control
    {
        juce::Label label;
        juce::Slider slider;
        juce::String key;
        bool automatic = false; // slider minimum means "auto" (calibrated value)
        bool rimOnly = false;
    };

    void timerCallback() override;
    void refresh();
    void update();
    void choose(const juce::String& id);
    void run(const juce::StringArray& args);
    void rebuildButtons();
    void send(Control& control);
    [[nodiscard]] const Pad* selected() const;
    [[nodiscard]] juce::String valueText(const Control& control) const;

    juce::Label title, subtitle, detail, modeLabel, meterTitle, measuredText;
    juce::TextButton close{"Close"}, reset{"Reset to calibration"};
    TouchChoice zoneMode;
    std::array<Control, 6> controls;
    juce::ChildProcess command;
    std::vector<Pad> pads;
    std::vector<std::unique_ptr<juce::TextButton>> padButtons;
    juce::Rectangle<int> meterBounds;
    juce::String selectedId, error, commandError;
    bool running = false, commandBusy = false, engineActive = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TriggerSettingsPanel)
};
