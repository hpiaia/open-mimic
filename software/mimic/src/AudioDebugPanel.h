// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "TouchChoice.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

class AudioDebugPanel final : public juce::Component, private juce::Timer
{
public:
    AudioDebugPanel();
    ~AudioDebugPanel() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;

    std::function<void()> onClose;

private:
    void play();
    void stop();
    void timerCallback() override;
    void updateControls();
    [[nodiscard]] juce::String channels() const;

    juce::Label title;
    juce::Label description;
    juce::Label routeLabel;
    TouchChoice route;
    juce::Label frequencyLabel;
    TouchChoice frequency;
    juce::Label levelLabel;
    TouchChoice level;
    juce::Label duration;
    juce::Label status;
    juce::TextButton back{"Back"};
    juce::TextButton playButton{"Play test tone"};
    juce::TextButton stopButton{"Stop"};
    juce::ChildProcess playback;
    juce::ChildProcess stopCommand;
    bool playing = false;
    bool stopping = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioDebugPanel)
};
