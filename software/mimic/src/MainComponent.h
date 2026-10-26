// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "AudioDebugPanel.h"
#include "AuxInputPanel.h"
#include "MidiPanel.h"
#include "NetworkMonitor.h"
#include "PadCalibrationPanel.h"
#include "SystemMonitor.h"
#include "WheelInput.h"

#include <juce_gui_basics/juce_gui_basics.h>

class MainComponent final : public juce::Component, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void recordTouch(const juce::MouseEvent& event);

private:
    void show(const juce::String& text);
    void timerCallback() override;
    void updateHeadphoneVolume();
    void toggleHeadphoneMute();

    struct TouchListener final : juce::MouseListener
    {
        explicit TouchListener(MainComponent& component) : owner(component) {}

        void mouseDown(const juce::MouseEvent& event) override
        {
            owner.recordTouch(event);
        }

        MainComponent& owner;
    };

    TouchListener touchListener{*this};
    const juce::Colour accent{0xff4de2ba};

    struct Theme final : juce::LookAndFeel_V4
    {
        juce::Font getTextButtonFont(juce::TextButton&, int height) override
        {
            return juce::Font(
                juce::FontOptions(juce::jmin(22.0f, static_cast<float>(height) * 0.35f)));
        }
    };

    Theme theme;
    NetworkMonitor network;
    SystemMonitor system;
    AudioDebugPanel audioDebug;
    AuxInputPanel auxInput;
    MidiPanel midiPanel;
    PadCalibrationPanel padCalibration;
    WheelInput wheel;
    juce::Slider headphoneVolume;
    juce::Label headphoneLevel;
    juce::TextButton headphoneMute{"Mute"};
    double unmutedVolume = 100.0;
    juce::Label title;
    juce::Label subtitle;
    juce::Label systemStatus;
    juce::Label networkTitle;
    juce::Label ssid;
    juce::Label ip;
    juce::Label connection;
    juce::Label message;
    juce::Label touchStatus;
    juce::TextButton calibrate{"Calibrate pads"};
    juce::TextButton triggers{"ADC monitor"};
    juce::TextButton padMidi{"Pad MIDI map"};
    juce::TextButton audio{"Audio test"};
    juce::TextButton aux{"Aux input"};
    juce::TextButton midi{"MIDI"};
    juce::Rectangle<int> networkBounds;
    juce::Point<int> lastTouch;
    int taps = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
