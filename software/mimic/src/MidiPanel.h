// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "TouchChoice.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

class MidiPanel final : public juce::Component, private juce::Timer
{
public:
    MidiPanel();
    ~MidiPanel() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;

    std::function<void()> onClose;

private:
    struct Port
    {
        juce::String id;
        juce::String name;
        bool input = false;
        bool output = false;
        bool connected = false;
    };

    void timerCallback() override;
    void refreshStatus();
    void rebuildPorts(const std::vector<Port>& ports);
    void updateControls();
    void applyPorts();
    void testNote();
    void runCommand(const juce::StringArray& command, const juce::String& pending);
    [[nodiscard]] juce::String portId(const TouchChoice& choice) const;
    void selectPort(TouchChoice& choice, const juce::String& id);

    juce::Label title;
    juce::Label description;
    juce::Label inputLabel;
    juce::Label outputLabel;
    juce::Label channelLabel;
    juce::Label noteLabel;
    juce::Label velocityLabel;
    juce::Label status;
    juce::Label activity;
    juce::Label events;
    TouchChoice input;
    TouchChoice output;
    TouchChoice channel;
    TouchChoice note;
    TouchChoice velocity;
    juce::TextButton back{"Back"};
    juce::TextButton apply{"Apply ports"};
    juce::TextButton sendNote{"Send test note"};
    juce::TextButton panic{"All notes off"};
    juce::ChildProcess command;
    std::vector<Port> knownPorts;
    juce::String currentInput;
    juce::String currentOutput;
    juce::String serviceError;
    juce::String commandStatus;
    bool serviceRunning = false;
    bool commandRunning = false;
    bool commandFailed = false;
    bool portsDirty = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiPanel)
};
