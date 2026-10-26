// SPDX-License-Identifier: AGPL-3.0-only
#include "AuxInputPanel.h"

#include <cmath>
#include <sstream>

AuxInputPanel::AuxInputPanel()
{
    setOpaque(true);
    for (auto* component : std::initializer_list<juce::Component*>{
             &title, &description, &level, &volume, &followHeadphones, &explanation, &back})
    {
        addAndMakeVisible(component);
    }
    for (auto* label : {&title, &description, &level, &explanation})
    {
        label->setColour(juce::Label::textColourId, juce::Colour(0xffedf3fa));
    }
    title.setText("Aux input", juce::dontSendNotification);
    title.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    description.setText("Headphone mix", juce::dontSendNotification);
    description.setColour(juce::Label::textColourId, juce::Colour(0xff9aaec4));
    level.setFont(juce::FontOptions(22.0f));
    explanation.setJustificationType(juce::Justification::topLeft);
    volume.setSliderStyle(juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volume.setRange(0.0, 100.0, 1.0);
    volume.setScrollWheelEnabled(false);
    volume.setColour(juce::Slider::trackColourId, juce::Colour(0xff4de2ba));
    volume.setColour(juce::Slider::thumbColourId, juce::Colour(0xff4de2ba));
    followHeadphones.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffedf3fa));
    followHeadphones.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff4de2ba));

    double gain = 1.0;
    int follow = 0;
    const juce::File settingsFile("/run/mimic-aux-input");
    if (settingsFile.existsAsFile())
    {
        std::istringstream input(settingsFile.loadFileAsString().toStdString());
        double storedGain = 0.0;
        int storedFollow = 0;
        if (input >> storedGain >> storedFollow && std::isfinite(storedGain) && storedGain >= 0.0
            && storedGain <= 1.0 && (storedFollow == 0 || storedFollow == 1))
        {
            gain = storedGain;
            follow = storedFollow;
        }
    }
    volume.setValue(gain * 100.0, juce::dontSendNotification);
    followHeadphones.setToggleState(follow != 0, juce::dontSendNotification);
    volume.onValueChange = [this] { updateSettings(); };
    followHeadphones.onClick = [this] { updateSettings(); };
    back.onClick = [this]
    {
        if (onClose)
        {
            onClose();
        }
    };
    updateSettings();
}

void AuxInputPanel::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff101720));
}

void AuxInputPanel::resized()
{
    auto area = getLocalBounds().reduced(0, 8);
    auto header = area.removeFromTop(48);
    back.setBounds(header.removeFromRight(110));
    title.setBounds(header);
    description.setBounds(area.removeFromTop(32));
    area.removeFromTop(24);
    level.setBounds(area.removeFromTop(36));
    volume.setBounds(area.removeFromTop(76));
    area.removeFromTop(24);
    followHeadphones.setBounds(area.removeFromTop(64));
    area.removeFromTop(16);
    explanation.setBounds(area.removeFromTop(90));
}

void AuxInputPanel::updateSettings()
{
    const auto percent = volume.getValue();
    const auto follow = followHeadphones.getToggleState();
    const auto decibels = percent > 0.0 ? 20.0 * std::log10(percent / 100.0) : 0.0;
    level.setText(percent > 0.0 ? "Aux level: " + juce::String(juce::roundToInt(percent)) + "% ("
                                      + juce::String(decibels, decibels == 0.0 ? 0 : 1) + " dB)"
                                : "Aux level: muted",
                  juce::dontSendNotification);
    const auto settings = juce::String(percent / 100.0, 6) + " " + (follow ? "1\n" : "0\n");
    if (settings != publishedSettings)
    {
        const juce::File settingsFile("/run/mimic-aux-input");
        juce::TemporaryFile pending(settingsFile);
        if (!pending.getFile().replaceWithText(settings)
            || !pending.overwriteTargetFileWithTemporary())
        {
            explanation.setColour(juce::Label::textColourId, juce::Colour(0xffff8585));
            explanation.setText("Could not update Aux input controls.", juce::dontSendNotification);
            juce::Logger::outputDebugString("Could not update Aux input controls");
            return;
        }
        publishedSettings = settings;
    }
    explanation.setColour(juce::Label::textColourId, juce::Colour(0xff4de2ba));
    explanation.setText(follow
                            ? "Linked: Aux level is followed by headphone volume before output.\n"
                              "Lowering or muting the headphones also lowers or mutes Aux input."
                            : "Independent: Aux level goes directly to the headphone output.\n"
                              "Changing headphone volume or mute does not affect Aux input.",
                        juce::dontSendNotification);
}
