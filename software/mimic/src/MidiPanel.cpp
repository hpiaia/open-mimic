// SPDX-License-Identifier: AGPL-3.0-only
#include "MidiPanel.h"

#include "MidiControl.h"

#include <algorithm>

namespace
{
const juce::Colour textColour{0xffedf3fa};
const juce::Colour mutedColour{0xff9aaec4};
const juce::Colour accent{0xff4de2ba};
const juce::Colour errorColour{0xffff8585};
constexpr int nonePort = 1;
constexpr int firstPort = 2;
} // namespace

MidiPanel::MidiPanel()
{
    setOpaque(true);
    for (auto* component : std::initializer_list<juce::Component*>{&title,
                                                                   &description,
                                                                   &inputLabel,
                                                                   &outputLabel,
                                                                   &channelLabel,
                                                                   &noteLabel,
                                                                   &velocityLabel,
                                                                   &status,
                                                                   &activity,
                                                                   &events,
                                                                   &input,
                                                                   &output,
                                                                   &channel,
                                                                   &note,
                                                                   &velocity,
                                                                   &back,
                                                                   &apply,
                                                                   &sendNote,
                                                                   &panic})
    {
        addAndMakeVisible(component);
    }
    for (auto* label : {&title,
                        &description,
                        &inputLabel,
                        &outputLabel,
                        &channelLabel,
                        &noteLabel,
                        &velocityLabel,
                        &status,
                        &activity,
                        &events})
    {
        label->setColour(juce::Label::textColourId, textColour);
    }
    title.setText("MIDI input and output", juce::dontSendNotification);
    title.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    description.setText("DIN and USB MIDI. Monitor events and send 250 ms test notes.",
                        juce::dontSendNotification);
    description.setColour(juce::Label::textColourId, mutedColour);
    description.setFont(juce::FontOptions(16.0f));
    inputLabel.setText("Input", juce::dontSendNotification);
    outputLabel.setText("Output", juce::dontSendNotification);
    channelLabel.setText("Test channel", juce::dontSendNotification);
    noteLabel.setText("Test note", juce::dontSendNotification);
    velocityLabel.setText("Velocity", juce::dontSendNotification);
    status.setColour(juce::Label::textColourId, accent);
    status.setFont(juce::FontOptions(16.0f));
    activity.setColour(juce::Label::textColourId, mutedColour);
    activity.setFont(juce::FontOptions(16.0f));
    events.setFont(juce::FontOptions(16.0f));
    events.setJustificationType(juce::Justification::topLeft);
    events.setText("Waiting for MIDI input...", juce::dontSendNotification);
    for (int number = 1; number <= 16; ++number)
    {
        channel.addItem("Channel " + juce::String(number), number);
    }
    channel.setSelectedId(10, juce::dontSendNotification);
    note.addItem("36 - Kick", 37);
    note.addItem("38 - Snare", 39);
    note.addItem("42 - Closed hi-hat", 43);
    note.addItem("46 - Open hi-hat", 47);
    note.addItem("49 - Crash", 50);
    note.setSelectedId(37, juce::dontSendNotification);
    for (int value : {32, 64, 96, 127})
    {
        velocity.addItem(juce::String(value), value);
    }
    velocity.setSelectedId(96, juce::dontSendNotification);
    rebuildPorts({});
    input.onChange = output.onChange = [this]
    {
        portsDirty = true;
        commandStatus.clear();
        commandFailed = false;
        updateControls();
    };
    apply.onClick = [this] { applyPorts(); };
    sendNote.onClick = [this] { testNote(); };
    panic.onClick = [this]
    { runCommand({"/usr/bin/mimicd", "midi-panic"}, "Sending all notes off..."); };
    back.onClick = [this]
    {
        if (onClose)
        {
            onClose();
        }
    };
    refreshStatus();
    updateControls();
    startTimerHz(5);
}

MidiPanel::~MidiPanel()
{
    stopTimer();
}

void MidiPanel::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff101720));
}

void MidiPanel::resized()
{
    auto area = getLocalBounds().reduced(0, 8);
    auto header = area.removeFromTop(44);
    back.setBounds(header.removeFromRight(110));
    title.setBounds(header);
    description.setBounds(area.removeFromTop(22));
    area.removeFromTop(8);
    auto ports = area.removeFromTop(76);
    auto inputArea = ports.removeFromLeft(ports.getWidth() / 2);
    inputArea.removeFromRight(12);
    ports.removeFromLeft(12);
    inputLabel.setBounds(inputArea.removeFromTop(26));
    input.setBounds(inputArea);
    outputLabel.setBounds(ports.removeFromTop(26));
    output.setBounds(ports);
    area.removeFromTop(10);
    auto test = area.removeFromTop(74);
    const auto third = (test.getWidth() - 32) / 3;
    auto channelArea = test.removeFromLeft(third);
    test.removeFromLeft(16);
    auto noteArea = test.removeFromLeft(third);
    test.removeFromLeft(16);
    channelLabel.setBounds(channelArea.removeFromTop(26));
    channel.setBounds(channelArea);
    noteLabel.setBounds(noteArea.removeFromTop(26));
    note.setBounds(noteArea);
    velocityLabel.setBounds(test.removeFromTop(26));
    velocity.setBounds(test);
    area.removeFromTop(10);
    auto actions = area.removeFromTop(48);
    const auto buttonWidth = (actions.getWidth() - 32) / 3;
    apply.setBounds(actions.removeFromLeft(buttonWidth));
    actions.removeFromLeft(16);
    sendNote.setBounds(actions.removeFromLeft(buttonWidth));
    actions.removeFromLeft(16);
    panic.setBounds(actions);
    area.removeFromTop(8);
    status.setBounds(area.removeFromTop(30));
    activity.setBounds(area.removeFromTop(28));
    events.setBounds(area);
}

juce::String MidiPanel::portId(const TouchChoice& choice) const
{
    const auto index = choice.getSelectedId() - firstPort;
    return index >= 0 && index < static_cast<int>(knownPorts.size())
               ? knownPorts[static_cast<std::size_t>(index)].id
               : "none";
}

void MidiPanel::selectPort(TouchChoice& choice, const juce::String& id)
{
    const auto port = std::find_if(knownPorts.begin(),
                                   knownPorts.end(),
                                   [&id](const Port& candidate) { return candidate.id == id; });
    choice.setSelectedId(port == knownPorts.end()
                             ? nonePort
                             : firstPort
                                   + static_cast<int>(std::distance(knownPorts.begin(), port)),
                         juce::dontSendNotification);
}

void MidiPanel::rebuildPorts(const std::vector<Port>& ports)
{
    input.clear();
    output.clear();
    input.addItem("None", nonePort);
    output.addItem("None", nonePort);
    knownPorts = ports;
    for (std::size_t index = 0; index < knownPorts.size(); ++index)
    {
        const auto& port = knownPorts[index];
        const auto name = port.name + (port.connected ? "" : " (disconnected)");
        const auto id = firstPort + static_cast<int>(index);
        if (port.input)
        {
            input.addItem(name, id);
        }
        if (port.output)
        {
            output.addItem(name, id);
        }
    }
    selectPort(input, currentInput);
    selectPort(output, currentOutput);
}

void MidiPanel::refreshStatus()
{
    const juce::File file(mimic::midiStatusPath);
    const auto age = juce::Time::getCurrentTime().toMilliseconds()
                     - file.getLastModificationTime().toMilliseconds();
    if (!file.existsAsFile() || file.getSize() > 64 * 1024 || age > 5000)
    {
        serviceRunning = false;
        serviceError = "MIDI service is not available";
        return;
    }
    const auto snapshot = juce::JSON::parse(file);
    if (!snapshot.isObject())
    {
        serviceRunning = false;
        serviceError = "Could not read MIDI service status";
        return;
    }
    serviceRunning = static_cast<bool>(snapshot.getProperty("running", false));
    serviceError = snapshot.getProperty("error", "").toString();
    if (!serviceRunning && serviceError.isEmpty())
    {
        serviceError = "MIDI service is stopped";
    }
    currentInput = snapshot.getProperty("selectedInput", "none").toString();
    currentOutput = snapshot.getProperty("selectedOutput", "none").toString();
    std::vector<Port> discovered;
    const auto portList = snapshot.getProperty("ports", {});
    if (const auto* ports = portList.getArray())
    {
        for (const auto& item : *ports)
        {
            discovered.push_back({item.getProperty("id", "").toString(),
                                  item.getProperty("name", "Unknown MIDI port").toString(),
                                  static_cast<bool>(item.getProperty("input", false)),
                                  static_cast<bool>(item.getProperty("output", false)),
                                  static_cast<bool>(item.getProperty("connected", false))});
        }
    }
    const auto samePorts =
        discovered.size() == knownPorts.size()
        && std::equal(discovered.begin(),
                      discovered.end(),
                      knownPorts.begin(),
                      [](const Port& first, const Port& second)
                      {
                          return first.id == second.id && first.name == second.name
                                 && first.input == second.input && first.output == second.output
                                 && first.connected == second.connected;
                      });
    if (!samePorts)
    {
        // Keep a pending selection while a different USB device is added or removed.
        const auto pendingInput = portId(input);
        const auto pendingOutput = portId(output);
        rebuildPorts(discovered);
        if (portsDirty)
        {
            selectPort(input, pendingInput);
            selectPort(output, pendingOutput);
        }
    }
    else if (!portsDirty)
    {
        selectPort(input, currentInput);
        selectPort(output, currentOutput);
    }
    activity.setText("IN " + snapshot.getProperty("rxMessages", 0).toString() + " messages     OUT "
                         + snapshot.getProperty("txMessages", 0).toString() + " messages",
                     juce::dontSendNotification);
    juce::StringArray lines;
    const auto recentEvents = snapshot.getProperty("events", {});
    if (const auto* recent = recentEvents.getArray())
    {
        for (int index = recent->size() - 1; index >= juce::jmax(0, recent->size() - 5); --index)
        {
            lines.add(recent->getReference(index).getProperty("text", "").toString());
        }
    }
    events.setText(lines.isEmpty() ? "Waiting for MIDI input..." : lines.joinIntoString("\n"),
                   juce::dontSendNotification);
}

void MidiPanel::updateControls()
{
    const bool ready = serviceRunning && !commandRunning;
    const bool outputConnected =
        std::any_of(knownPorts.begin(),
                    knownPorts.end(),
                    [this](const Port& port)
                    { return port.id == currentOutput && port.output && port.connected; });
    apply.setEnabled(ready && portsDirty);
    sendNote.setEnabled(ready && outputConnected && !portsDirty);
    panic.setEnabled(ready && outputConnected);
    input.setEnabled(ready);
    output.setEnabled(ready);
    status.setColour(juce::Label::textColourId,
                     serviceError.isNotEmpty() || commandFailed ? errorColour : accent);
    status.setText(serviceError.isNotEmpty()    ? serviceError
                   : commandStatus.isNotEmpty() ? commandStatus
                   : portsDirty ? "Apply the selected ports to connect."
                                : "Ready. MIDI input is monitored without forwarding.",
                   juce::dontSendNotification);
}

void MidiPanel::runCommand(const juce::StringArray& arguments, const juce::String& pending)
{
    if (commandRunning)
    {
        return;
    }
    commandRunning = command.start(arguments);
    commandFailed = !commandRunning;
    commandStatus = commandRunning ? pending : "Could not start MIDI command";
    updateControls();
}

void MidiPanel::applyPorts()
{
    runCommand(
        {"/usr/bin/mimicd", "midi-config", "--input", portId(input), "--output", portId(output)},
        "Connecting selected MIDI ports...");
    if (commandRunning)
    {
        portsDirty = false;
    }
}

void MidiPanel::testNote()
{
    runCommand({"/usr/bin/mimicd",
                "midi-test",
                "--note",
                juce::String(note.getSelectedId() - 1),
                "--velocity",
                juce::String(velocity.getSelectedId()),
                "--channel",
                juce::String(channel.getSelectedId()),
                "--ms",
                "250"},
               "Sending test note (250 ms)...");
}

void MidiPanel::timerCallback()
{
    if (!isShowing() && !commandRunning)
    {
        return;
    }
    if (commandRunning && !command.isRunning())
    {
        const auto result = command.getExitCode();
        const auto outputText = command.readAllProcessOutput().trim().substring(0, 160);
        commandStatus = result == 0 ? "Command accepted" : "MIDI command failed: " + outputText;
        commandFailed = result != 0;
        commandRunning = false;
    }
    refreshStatus();
    updateControls();
}
