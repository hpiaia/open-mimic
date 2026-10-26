// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "NetworkMonitor.h"
#include "SystemMonitor.h"

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
    juce::TextButton triggers{"Trigger monitor"};
    juce::TextButton audio{"Audio test"};
    juce::TextButton touch{"Touch test"};
    juce::Rectangle<int> networkBounds;
    juce::Point<int> lastTouch;
    int taps = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
