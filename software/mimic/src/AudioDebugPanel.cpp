// SPDX-License-Identifier: AGPL-3.0-only
#include "AudioDebugPanel.h"

#include "AudioRouting.h"

AudioDebugPanel::AudioDebugPanel()
{
    setOpaque(true);
    for (auto* component : std::initializer_list<juce::Component*>{&title,
                                                                   &description,
                                                                   &routeLabel,
                                                                   &route,
                                                                   &frequencyLabel,
                                                                   &frequency,
                                                                   &levelLabel,
                                                                   &level,
                                                                   &duration,
                                                                   &status,
                                                                   &back,
                                                                   &playButton,
                                                                   &stopButton})
    {
        addAndMakeVisible(component);
    }
    for (auto* label :
         {&title, &description, &routeLabel, &frequencyLabel, &levelLabel, &duration, &status})
    {
        label->setColour(juce::Label::textColourId, juce::Colour(0xffedf3fa));
    }
    title.setText("Audio output test", juce::dontSendNotification);
    title.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    description.setText("Play a short sine wave on the selected output.",
                        juce::dontSendNotification);
    routeLabel.setText("Output", juce::dontSendNotification);
    frequencyLabel.setText("Frequency", juce::dontSendNotification);
    levelLabel.setText("Test level", juce::dontSendNotification);
    duration.setText("Duration: 3 seconds", juce::dontSendNotification);
    duration.setColour(juce::Label::textColourId, juce::Colour(0xff9aaec4));
    status.setText("Ready", juce::dontSendNotification);
    status.setColour(juce::Label::textColourId, juce::Colour(0xff4de2ba));
    route.addItem("Headphones - stereo", 1);
    route.addItem("Headphones - left", 2);
    route.addItem("Headphones - right", 3);
    route.addItem("Master - stereo", 4);
    route.addItem("Master - left", 5);
    route.addItem("Master - right", 6);
    for (int channel = 0; channel < 16; ++channel)
    {
        route.addItem("Hardware channel " + juce::String(channel), 10 + channel);
    }
    route.setSelectedId(1, juce::dontSendNotification);
    frequency.addItem("220 Hz", 220);
    frequency.addItem("440 Hz", 440);
    frequency.addItem("880 Hz", 880);
    frequency.addItem("1000 Hz", 1000);
    frequency.setSelectedId(440, juce::dontSendNotification);
    level.addItem("0 dBFS", 1);
    level.addItem("-40 dBFS", 40);
    level.addItem("-30 dBFS", 30);
    level.addItem("-20 dBFS", 20);
    level.setSelectedId(1, juce::dontSendNotification);
    playButton.onClick = [this] { play(); };
    stopButton.onClick = [this] { stop(); };
    back.onClick = [this]
    {
        stop();
        if (onClose)
        {
            onClose();
        }
    };
    updateControls();
    startTimerHz(10);
}

AudioDebugPanel::~AudioDebugPanel()
{
    stopTimer();
    if (playback.isRunning())
    {
        stop();
        stopCommand.waitForProcessToFinish(1000);
        if (!playback.waitForProcessToFinish(2000))
        {
            playback.kill();
        }
    }
}

void AudioDebugPanel::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff101720));
}

void AudioDebugPanel::resized()
{
    auto area = getLocalBounds().reduced(0, 8);
    auto header = area.removeFromTop(48);
    back.setBounds(header.removeFromRight(110));
    title.setBounds(header);
    description.setBounds(area.removeFromTop(32));
    area.removeFromTop(22);
    auto controls = area.removeFromTop(88);
    auto output = controls.removeFromLeft(controls.getWidth() / 2);
    output.removeFromRight(24);
    routeLabel.setBounds(output.removeFromTop(28));
    route.setBounds(output.removeFromTop(54));
    auto frequencyArea = controls.removeFromLeft(controls.getWidth() / 2);
    frequencyArea.removeFromRight(20);
    frequencyLabel.setBounds(frequencyArea.removeFromTop(28));
    frequency.setBounds(frequencyArea.removeFromTop(54));
    levelLabel.setBounds(controls.removeFromTop(28));
    level.setBounds(controls.removeFromTop(54));
    area.removeFromTop(12);
    duration.setBounds(area.removeFromTop(32));
    area.removeFromTop(24);
    auto buttons = area.removeFromTop(64);
    playButton.setBounds(buttons.removeFromLeft(260));
    buttons.removeFromLeft(16);
    stopButton.setBounds(buttons.removeFromLeft(180));
    area.removeFromTop(20);
    status.setBounds(area.removeFromTop(60));
}

juce::String AudioDebugPanel::channels() const
{
    switch (route.getSelectedId())
    {
    case 1:
        return juce::String(mimic::headphoneLeftChannel) + ","
               + juce::String(mimic::headphoneRightChannel);
    case 2:
        return juce::String(mimic::headphoneLeftChannel);
    case 3:
        return juce::String(mimic::headphoneRightChannel);
    case 4:
        return juce::String(mimic::masterLeftChannel) + ","
               + juce::String(mimic::masterRightChannel);
    case 5:
        return juce::String(mimic::masterLeftChannel);
    case 6:
        return juce::String(mimic::masterRightChannel);
    default:
        return juce::String(route.getSelectedId() - 10);
    }
}

void AudioDebugPanel::play()
{
    if (playing)
    {
        return;
    }
    const juce::StringArray command{"/usr/bin/mimicd",
                                    "audio-test",
                                    "--channels",
                                    channels(),
                                    "--frequency",
                                    juce::String(frequency.getSelectedId()),
                                    "--seconds",
                                    "3",
                                    "--amplitude",
                                    level.getSelectedId() == 1    ? "1.0"
                                    : level.getSelectedId() == 20 ? "0.1"
                                    : level.getSelectedId() == 30 ? "0.0316228"
                                                                  : "0.01"};
    if (!playback.start(command))
    {
        status.setText("Could not start audio test", juce::dontSendNotification);
        return;
    }
    playing = true;
    stopping = false;
    status.setText("Playing test tone...", juce::dontSendNotification);
    updateControls();
}

void AudioDebugPanel::stop()
{
    if (!playing || stopping)
    {
        return;
    }
    stopping = stopCommand.start(juce::StringArray{"/usr/bin/mimicd", "audio-stop"});
    status.setText(stopping ? "Stopping..." : "Could not request stop; test ends after 3 seconds",
                   juce::dontSendNotification);
    updateControls();
}

void AudioDebugPanel::updateControls()
{
    playButton.setEnabled(!playing);
    stopButton.setEnabled(playing && !stopping);
    route.setEnabled(!playing);
    frequency.setEnabled(!playing);
    level.setEnabled(!playing);
}

void AudioDebugPanel::timerCallback()
{
    // Poll the cancellation helper too, so completed children are reaped.
    stopCommand.isRunning();
    if (playing && !playback.isRunning())
    {
        const auto result = playback.getExitCode();
        const auto output = playback.readAllProcessOutput().trim();
        if (output.isNotEmpty())
        {
            juce::Logger::outputDebugString(output);
        }
        if (result == 0)
        {
            status.setText(stopping ? "Stopped" : "Test complete", juce::dontSendNotification);
        }
        else
        {
            status.setText("Audio test failed: " + output.substring(0, 160),
                           juce::dontSendNotification);
        }
        playing = false;
        stopping = false;
        updateControls();
    }
}
