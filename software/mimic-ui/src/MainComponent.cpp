// SPDX-License-Identifier: AGPL-3.0-only
#include "MainComponent.h"

MainComponent::MainComponent()
{
    theme.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1c2836));
    theme.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffedf3fa));
    setLookAndFeel(&theme);
    for (auto* label :
         {&title, &subtitle, &networkTitle, &ssid, &ip, &connection, &message, &touchStatus})
    {
        addAndMakeVisible(*label);
        label->setColour(juce::Label::textColourId, juce::Colour(0xffedf3fa));
    }
    addAndMakeVisible(systemStatus);
    systemStatus.setFont(juce::FontOptions(14.0f));
    systemStatus.setColour(juce::Label::textColourId, juce::Colour(0xff9aaec4));
    systemStatus.setJustificationType(juce::Justification::centredRight);
    systemStatus.setText("CPU --     RAM --", juce::dontSendNotification);
    title.setText("OPEN MIMIC", juce::dontSendNotification);
    title.setFont(juce::FontOptions(34.0f, juce::Font::bold));
    subtitle.setText("Instrument control", juce::dontSendNotification);
    networkTitle.setText("WIRELESS NETWORK", juce::dontSendNotification);
    ssid.setFont(juce::FontOptions(29.0f));
    ssid.setColour(juce::Label::textColourId, accent);
    ip.setFont(juce::FontOptions(20.0f));
    connection.setJustificationType(juce::Justification::centredRight);
    message.setText("Select a function", juce::dontSendNotification);
    message.setColour(juce::Label::textColourId, accent);
    touchStatus.setText("Touch ready", juce::dontSendNotification);
    for (auto* button : {&calibrate, &triggers, &audio, &touch})
    {
        addAndMakeVisible(*button);
        button->setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    calibrate.onClick = [this] { show("Pad calibration is not implemented yet."); };
    triggers.onClick = [this] { show("Trigger monitor is not implemented yet."); };
    audio.onClick = [this] { show("Audio output is not connected yet."); };
    touch.onClick = [this] { show("Touch input received."); };
    addMouseListener(&touchListener, true);
    setSize(1024, 600);
    startTimerHz(5);
}

MainComponent::~MainComponent()
{
    stopTimer();
    removeMouseListener(&touchListener);
    setLookAndFeel(nullptr);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff101720));
    g.setColour(accent);
    g.fillRect(0, 0, 6, getHeight());
    g.setColour(juce::Colour(0xff1c2836));
    g.fillRoundedRectangle(networkBounds.toFloat(), 12.0f);
}

void MainComponent::paintOverChildren(juce::Graphics& g)
{
    if (taps > 0)
    {
        g.setColour(accent.withAlpha(0.8f));
        g.drawEllipse(static_cast<float>(lastTouch.x - 9),
                      static_cast<float>(lastTouch.y - 9),
                      18.0f,
                      18.0f,
                      2.0f);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(36, 20);
    auto header = area.removeFromTop(43);
    systemStatus.setBounds(header.removeFromRight(340));
    title.setBounds(header);
    subtitle.setBounds(area.removeFromTop(27));
    area.removeFromTop(14);
    networkBounds = area.removeFromTop(145);
    auto card = networkBounds.reduced(20, 12);
    networkTitle.setBounds(card.removeFromTop(25));
    ssid.setBounds(card.removeFromTop(48));
    auto row = card.removeFromBottom(30);
    connection.setBounds(row.removeFromRight(row.getWidth() / 3));
    ip.setBounds(row);
    area.removeFromTop(24);
    auto buttons = area.removeFromTop(juce::jmin(194, area.getHeight() - 76));
    juce::Grid grid;
    using Track = juce::Grid::TrackInfo;
    using Fr = juce::Grid::Fr;
    grid.templateColumns = {Track(Fr(1)), Track(Fr(1))};
    grid.templateRows = {Track(Fr(1)), Track(Fr(1))};
    grid.setGap(juce::Grid::Px(16));
    grid.items = {juce::GridItem(calibrate),
                  juce::GridItem(triggers),
                  juce::GridItem(audio),
                  juce::GridItem(touch)};
    grid.performLayout(buttons);
    area.removeFromTop(12);
    message.setBounds(area.removeFromTop(30));
    touchStatus.setBounds(area.removeFromBottom(26));
}

void MainComponent::recordTouch(const juce::MouseEvent& event)
{
    lastTouch = event.getEventRelativeTo(this).getPosition();
    ++taps;
    touchStatus.setText("Touch events: " + juce::String(taps) + "   Position: "
                            + juce::String(lastTouch.x) + ", " + juce::String(lastTouch.y),
                        juce::dontSendNotification);
    repaint();
}

void MainComponent::show(const juce::String& text)
{
    message.setText(text, juce::dontSendNotification);
}

void MainComponent::timerCallback()
{
    const auto usage = system.snapshot();
    const auto cpu =
        usage.cpuPercent ? juce::String(juce::roundToInt(*usage.cpuPercent)) + "%" : "--";
    const auto ram =
        usage.memory
            ? juce::String(static_cast<juce::int64>((usage.memory->usedKiB + 512) / 1024)) + " / "
                  + juce::String(static_cast<juce::int64>((usage.memory->totalKiB + 512) / 1024))
                  + " MiB"
            : "--";
    systemStatus.setText("CPU " + cpu + "     RAM " + ram, juce::dontSendNotification);

    const auto state = network.snapshot();
    ssid.setText(state.ssid, juce::dontSendNotification);
    ip.setText("IP: " + state.address, juce::dontSendNotification);
    connection.setText(state.state, juce::dontSendNotification);
}
