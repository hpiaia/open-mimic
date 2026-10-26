// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

class AuxInputPanel final : public juce::Component
{
public:
    AuxInputPanel();
    void paint(juce::Graphics& graphics) override;
    void resized() override;

    std::function<void()> onClose;

private:
    void updateSettings();

    juce::Label title;
    juce::Label description;
    juce::Label level;
    juce::Slider volume;
    juce::ToggleButton followHeadphones{"Follow headphone volume"};
    juce::Label explanation;
    juce::TextButton back{"Back"};
    juce::String publishedSettings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AuxInputPanel)
};
