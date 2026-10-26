// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "TouchChoice.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

// Three independent screens share only the service transport and pad definitions.
class PadCalibrationPanel final : public juce::Component, private juce::Timer
{
public:
    PadCalibrationPanel();
    ~PadCalibrationPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void showCalibration();
    void showMonitor();
    void showMapping();
    std::function<void()> onClose;

private:
    enum class Mode
    {
        calibration,
        monitor,
        mapping
    };
    enum class Page
    {
        selection,
        setup,
        wizard,
        mapping
    };

    struct Pad
    {
        juce::String id, name, instrument;
        int zones = 1, head = -1, rim = -1, channel = 10;
        int headNote = -1, rimNote = -1, rimshotNote = -1;
    };

    void timerCallback() override;
    void refresh();
    void update();
    void open(Mode);
    void choose(const juce::String&);
    void run(const juce::StringArray&);
    void back();
    void nextPad();
    void rebuildButtons();
    [[nodiscard]] const Pad* selected() const;

    Mode mode = Mode::calibration;
    Page page = Page::selection;
    juce::Label title, subtitle, instruction, counter, detail, channelLabel, headLabel, rimLabel,
        shotLabel;
    juce::TextButton liveMidi{"Start MIDI"};
    juce::TextButton close{"Close"}, start{"Start calibration"}, stop{"Stop calibration"},
        next{"Next pad"}, save{"Save MIDI map"};
    TouchChoice zones, channel, headNote, rimNote, rimshotNote;
    juce::ChildProcess command;
    std::vector<Pad> pads;
    std::vector<std::unique_ptr<juce::TextButton>> padButtons;
    std::array<int, 32> meters{};
    juce::Rectangle<int> monitorBounds;
    juce::String selectedId, servicePad, state, phase, upload, error, archiveError, commandError;
    int captured = 0, target = 12, step = 0, steps = 0, countdown = 0;
    bool continuous = false;
    bool engineEnabled = false;
    bool running = false, active = false, archiveOnline = false, commandBusy = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadCalibrationPanel)
};
