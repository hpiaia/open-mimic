// SPDX-License-Identifier: AGPL-3.0-only
#include "PadCalibrationPanel.h"

#include "PadControl.h"
#include "PadRouting.h"

#include <algorithm>

namespace
{
const juce::Colour text{0xffedf3fa}, muted{0xff9aaec4}, accent{0xff4de2ba};

juce::String instructionsFor(const juce::String& phase)
{
    if (phase == "ambient")
    {
        return "Keep the kit still for a moment.";
    }
    const auto area = phase.startsWith("rimshot") ? "rimshots: head and rim together"
                      : phase.startsWith("rim")   ? "rim-only hits"
                                                  : "head hits";
    return "Play " + juce::String(area) + ". Vary from very light to strong; rolls are welcome.";
}

juce::String phaseTitle(const juce::String& phase)
{
    if (phase == "ambient")
    {
        return "Quiet kit";
    }
    if (phase == "xtalk")
    {
        return "Other pads";
    }
    return phase.replace("-varied", "").toUpperCase();
}
} // namespace

PadCalibrationPanel::PadCalibrationPanel()
{
    setOpaque(true);
    for (auto* component : std::initializer_list<juce::Component*>{&title,
                                                                   &subtitle,
                                                                   &instruction,
                                                                   &counter,
                                                                   &detail,
                                                                   &close,
                                                                   &start,
                                                                   &stop,
                                                                   &next,
                                                                   &save,
                                                                   &zones,
                                                                   &channel,
                                                                   &headNote,
                                                                   &rimNote,
                                                                   &rimshotNote,
                                                                   &channelLabel,
                                                                   &headLabel,
                                                                   &rimLabel,
                                                                   &shotLabel})
    {
        addAndMakeVisible(*component);
    }
    for (auto* label : {&title,
                        &subtitle,
                        &instruction,
                        &counter,
                        &detail,
                        &channelLabel,
                        &headLabel,
                        &rimLabel,
                        &shotLabel})
    {
        label->setColour(juce::Label::textColourId, text);
    }
    title.setFont(juce::FontOptions(29.0f, juce::Font::bold));
    subtitle.setColour(juce::Label::textColourId, muted);
    instruction.setFont(juce::FontOptions(27.0f));
    instruction.setJustificationType(juce::Justification::centred);
    counter.setFont(juce::FontOptions(44.0f, juce::Font::bold));
    counter.setJustificationType(juce::Justification::centred);
    counter.setColour(juce::Label::textColourId, accent);
    detail.setColour(juce::Label::textColourId, muted);
    detail.setJustificationType(juce::Justification::centred);
    zones.addItem("Single zone", 1);
    zones.addItem("Dual zone", 2);
    for (int value = 1; value <= 16; ++value)
    {
        channel.addItem(juce::String(value), value);
    }
    for (auto* choice : {&headNote, &rimNote, &rimshotNote})
    {
        choice->addItem("Off", 1);
        for (int note = 0; note < 128; ++note)
        {
            choice->addItem(juce::String(note), note + 2);
        }
    }
    channelLabel.setText("MIDI channel", juce::dontSendNotification);
    headLabel.setText("Head / center", juce::dontSendNotification);
    rimLabel.setText("Rim only", juce::dontSendNotification);
    shotLabel.setText("Rimshot", juce::dontSendNotification);
    addAndMakeVisible(liveMidi);
    liveMidi.onClick = [this] { run({"engine", engineEnabled ? "off" : "on"}); };
    close.onClick = [this] { back(); };
    start.onClick = [this]
    {
        if (const auto* pad = selected())
        {
            run({"wizard", pad->id, juce::String(zones.getSelectedId())});
            page = Page::wizard;
            update();
            resized();
        }
    };
    stop.onClick = [this] { run({"stop"}); };
    next.onClick = [this] { nextPad(); };
    save.onClick = [this]
    {
        if (const auto* pad = selected())
        {
            run({"settings",
                 pad->id,
                 juce::String(pad->zones),
                 juce::String(channel.getSelectedId()),
                 juce::String(headNote.getSelectedId() - 2),
                 juce::String(rimNote.getSelectedId() - 2),
                 juce::String(rimshotNote.getSelectedId() - 2)});
        }
    };
    startTimerHz(5);
}

PadCalibrationPanel::~PadCalibrationPanel()
{
    stopTimer();
}

void PadCalibrationPanel::open(Mode value)
{
    mode = value;
    page = Page::selection;
    commandError.clear();
    refresh();
    if (mode == Mode::calibration
        && (active || state == "complete" || state == "countdown" || state == "sequence-writing"))
    {
        selectedId = servicePad;
        page = Page::wizard;
    }
    update();
    resized();
}

void PadCalibrationPanel::showCalibration()
{
    open(Mode::calibration);
}

void PadCalibrationPanel::showMonitor()
{
    open(Mode::monitor);
}

void PadCalibrationPanel::showMapping()
{
    open(Mode::mapping);
}

const PadCalibrationPanel::Pad* PadCalibrationPanel::selected() const
{
    const auto found =
        std::find_if(pads.begin(), pads.end(), [this](const Pad& p) { return p.id == selectedId; });
    return found == pads.end() ? nullptr : &*found;
}

void PadCalibrationPanel::choose(const juce::String& id)
{
    selectedId = id;
    if (const auto* p = selected())
    {
        zones.setSelectedId(p->zones, juce::dontSendNotification);
        channel.setSelectedId(p->channel, juce::dontSendNotification);
        headNote.setSelectedId(p->headNote + 2, juce::dontSendNotification);
        rimNote.setSelectedId(p->rimNote + 2, juce::dontSendNotification);
        rimshotNote.setSelectedId(p->rimshotNote + 2, juce::dontSendNotification);
    }
    page = mode == Mode::mapping ? Page::mapping : Page::setup;
    commandError.clear();
    update();
    resized();
}

void PadCalibrationPanel::nextPad()
{
    const auto found = std::find_if(
        pads.begin(), pads.end(), [this](const Pad& pad) { return pad.id == selectedId; });
    auto following = found == pads.end() ? pads.end() : std::next(found);
    while (following != pads.end() && following->head < 0)
    {
        ++following;
    }
    if (following == pads.end())
    {
        page = Page::selection;
        update();
        resized();
    }
    else
    {
        choose(following->id);
    }
}

void PadCalibrationPanel::back()
{
    if (mode == Mode::calibration && page == Page::wizard && active)
    {
        run({"stop"});
        return;
    }
    if (page != Page::selection && mode != Mode::monitor)
    {
        page = Page::selection;
        update();
        resized();
        return;
    }
    if (onClose)
    {
        onClose();
    }
}

void PadCalibrationPanel::run(const juce::StringArray& args)
{
    if (commandBusy)
    {
        return;
    }
    juce::StringArray arguments{"/usr/bin/mimicd", "pad-command"};
    arguments.addArray(args);
    commandError.clear();
    commandBusy = command.start(arguments);
    if (!commandBusy)
    {
        commandError = "Could not send command to the pad service.";
    }
}

void PadCalibrationPanel::timerCallback()
{
    if (!isVisible())
    {
        return;
    }
    if (commandBusy && !command.isRunning())
    {
        const auto output = command.readAllProcessOutput().trim();
        if (command.getExitCode() != 0)
        {
            commandError = output;
        }
        commandBusy = false;
    }
    refresh();
    update();
    repaint();
}

void PadCalibrationPanel::refresh()
{
    const auto statusFile = juce::File(mimic::padStatusPath);
    if (!statusFile.existsAsFile() || statusFile.getSize() > 65536)
    {
        running = false;
        return;
    }
    const auto data = juce::JSON::parse(statusFile.loadFileAsString());
    engineEnabled = static_cast<bool>(data.getProperty("engine", {}).getProperty("enabled", false));
    running = static_cast<bool>(data.getProperty("running", false));
    archiveError = data.getProperty("archiveError", "").toString();
    error = data.getProperty("error", "").toString();
    state = data.getProperty("state", "").toString();
    phase = data.getProperty("phase", "").toString();
    servicePad = data.getProperty("selectedPad", "").toString();
    active = static_cast<bool>(data.getProperty("wizardActive", false)) || state == "capturing";
    continuous = static_cast<bool>(data.getProperty("continuous", false));
    captured = data.getProperty("captured", 0);
    target = data.getProperty("target", 12);
    step = data.getProperty("wizardStep", 0);
    steps = data.getProperty("wizardSteps", 0);
    countdown = data.getProperty("countdown", 0);
    upload = data.getProperty("uploadState", "idle").toString();
    archiveOnline = static_cast<bool>(data.getProperty("archiveConnected", false));
    if (const auto* values = data.getProperty("meters", {}).getArray())
    {
        for (int i = 0; i < juce::jmin(32, values->size()); ++i)
        {
            meters[static_cast<std::size_t>(i)] = static_cast<int>(values->getReference(i));
        }
    }
    std::vector<Pad> updated;
    if (const auto* values = data.getProperty("pads", {}).getArray())
    {
        for (const auto& p : *values)
        {
            updated.push_back({p["id"].toString(),
                               p["name"].toString(),
                               p["sd3Instrument"].toString(),
                               static_cast<int>(p["zoneCount"]),
                               static_cast<int>(p["head"]),
                               static_cast<int>(p["rim"]),
                               static_cast<int>(p["midiChannel"]),
                               static_cast<int>(p["headNote"]),
                               static_cast<int>(p["rimNote"]),
                               static_cast<int>(p["rimshotNote"])});
        }
    }
    const bool rebuild = pads.size() != updated.size();
    pads = std::move(updated);
    if (rebuild)
    {
        rebuildButtons();
    }
}

void PadCalibrationPanel::rebuildButtons()
{
    padButtons.clear();
    for (const auto& pad : pads)
    {
        auto button = std::make_unique<juce::TextButton>(pad.name);
        const auto id = pad.id;
        button->onClick = [this, id] { choose(id); };
        addAndMakeVisible(*button);
        padButtons.push_back(std::move(button));
    }
    resized();
}

void PadCalibrationPanel::update()
{
    const auto* p = selected();
    liveMidi.setVisible(mode == Mode::mapping);
    liveMidi.setButtonText(engineEnabled ? "Stop MIDI" : "Start MIDI");
    liveMidi.setEnabled(running && !commandBusy);
    const bool grid = page == Page::selection && mode != Mode::monitor;
    const bool setup = page == Page::setup && mode == Mode::calibration;
    const bool wizard = page == Page::wizard && mode == Mode::calibration;
    const bool mapping = page == Page::mapping && mode == Mode::mapping;
    const bool done = wizard && state == "complete" && servicePad == selectedId;
    title.setText(mode == Mode::monitor   ? "ADC monitor"
                  : mode == Mode::mapping ? "Pad MIDI map"
                                          : "Pad calibration",
                  juce::dontSendNotification);
    subtitle.setText(grid                    ? "Choose a pad"
                     : mode == Mode::monitor ? "Physical channel IDs. Hardware routing is fixed."
                     : p ? p->name + (mapping ? "  /  SD3 " + p->instrument : "")
                         : "",
                     juce::dontSendNotification);
    close.setButtonText(page == Page::selection || mode == Mode::monitor ? "Close" : "Back");
    for (std::size_t i = 0; i < padButtons.size(); ++i)
    {
        padButtons[i]->setVisible(grid);
        padButtons[i]->setEnabled(running && !active
                                  && (mode == Mode::mapping || pads[i].head >= 0));
    }
    zones.setVisible(setup);
    start.setVisible(setup);
    start.setEnabled(running && !commandBusy && p && p->head >= 0 && !active);
    stop.setVisible(wizard && active);
    stop.setEnabled(!commandBusy);
    next.setVisible(done);
    next.setEnabled(upload == "uploaded" && !commandBusy);
    for (auto* c : std::initializer_list<juce::Component*>{&channel,
                                                           &headNote,
                                                           &rimNote,
                                                           &rimshotNote,
                                                           &channelLabel,
                                                           &headLabel,
                                                           &rimLabel,
                                                           &shotLabel,
                                                           &save})
    {
        c->setVisible(mapping);
    }
    save.setEnabled(running && !active && !commandBusy);
    instruction.setVisible(setup || wizard);
    counter.setVisible(wizard);
    if (setup)
    {
        instruction.setText(
            "Choose the sensor type. Each articulation is recorded continuously for 20 seconds.",
            juce::dontSendNotification);
    }
    if (wizard)
    {
        instruction.setText(done ? "Calibration recorded" : instructionsFor(phase),
                            juce::dontSendNotification);
        counter.setText(done ? (upload == "uploaded" ? "Saved on Mac" : "Saving to Mac...")
                        : state == "countdown"        ? "Get ready: " + juce::String(countdown)
                        : state == "sequence-writing" ? "Saving..."
                        : continuous
                            ? juce::String(captured) + " / " + juce::String(target) + " seconds"
                            : juce::String(captured) + " / " + juce::String(target),
                        juce::dontSendNotification);
    }
    juce::String footer =
        mapping ? "SD3 defaults: center, rim only and rimshot. Notes are editable." : "";
    if (wizard)
    {
        footer =
            done ? (upload == "uploaded" ? "Verified transfer complete. Ready for the next pad."
                    : archiveOnline
                        ? "Keep the module connected while this recording is verified."
                        : "Waiting for the Mac capture receiver. Your recordings remain on the SD.")
                 : (phase == "ambient" ? juce::String("Preparing")
                                       : "Step " + juce::String(step) + " / "
                                             + juce::String(juce::jmax(1, steps - 1)))
                       + "  -  " + phaseTitle(phase);
    }
    if (setup)
    {
        footer =
            archiveOnline
                ? "Mac receiver connected. Recordings transfer automatically after this pad."
                : "Mac receiver offline. Recordings will remain on the SD until it reconnects.";
    }
    if (done && upload != "uploaded" && !archiveError.isEmpty())
    {
        footer = archiveError;
    }
    if (!running)
    {
        footer = "Pad service is unavailable.";
    }
    if (!error.isEmpty())
    {
        footer = error;
    }
    if (!commandError.isEmpty())
    {
        footer = commandError;
    }
    detail.setText(footer, juce::dontSendNotification);
}

void PadCalibrationPanel::resized()
{
    auto area = getLocalBounds().reduced(20, 12);
    auto header = area.removeFromTop(46);
    close.setBounds(header.removeFromRight(112));
    if (mode == Mode::mapping)
    {
        liveMidi.setBounds(header.removeFromRight(140).reduced(6, 0));
    }
    title.setBounds(header);
    subtitle.setBounds(area.removeFromTop(30));
    detail.setBounds(area.removeFromBottom(55));
    area.removeFromTop(14);
    monitorBounds = area;
    const int width = (area.getWidth() - 24) / 3;
    for (std::size_t i = 0; i < padButtons.size(); ++i)
    {
        padButtons[i]->setBounds(area.getX() + static_cast<int>(i % 3) * (width + 12),
                                 area.getY() + static_cast<int>(i / 3) * 82,
                                 width,
                                 70);
    }
    if (page == Page::setup)
    {
        instruction.setBounds(area.removeFromTop(100));
        area.removeFromTop(25);
        zones.setBounds(area.getCentreX() - 160, area.getY(), 320, 56);
        area.removeFromTop(85);
        start.setBounds(area.getCentreX() - 180, area.getY(), 360, 70);
    }
    else if (page == Page::wizard)
    {
        instruction.setBounds(area.removeFromTop(110));
        counter.setBounds(area.removeFromTop(100));
        area.removeFromTop(25);
        next.setBounds(area.getCentreX() - 160, area.getY(), 320, 60);
        stop.setBounds(next.getBounds());
    }
    else if (page == Page::mapping)
    {
        auto row = area.removeFromTop(98);
        const int cell = (row.getWidth() - 36) / 4;
        std::array<juce::Label*, 4> labels{&channelLabel, &headLabel, &rimLabel, &shotLabel};
        std::array<TouchChoice*, 4> choices{&channel, &headNote, &rimNote, &rimshotNote};
        for (std::size_t i = 0; i < labels.size(); ++i)
        {
            auto field = row.removeFromLeft(cell);
            labels[i]->setBounds(field.removeFromTop(32));
            choices[i]->setBounds(field);
            row.removeFromLeft(12);
        }
        area.removeFromTop(34);
        save.setBounds(area.getCentreX() - 180, area.getY(), 360, 62);
    }
}

void PadCalibrationPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff101820));
    if (mode != Mode::monitor)
    {
        return;
    }
    for (unsigned adc = 0; adc < 2; ++adc)
    {
        auto panel = monitorBounds.withWidth(monitorBounds.getWidth() / 2 - 8);
        panel.setX(monitorBounds.getX()
                   + static_cast<int>(adc) * (monitorBounds.getWidth() / 2 + 8));
        g.setColour(text);
        g.setFont(18.0f);
        g.drawText(
            "ADC " + juce::String(adc), panel.removeFromTop(28), juce::Justification::centred);
        const int cellWidth = panel.getWidth() / 4, cellHeight = panel.getHeight() / 4;
        for (unsigned channelIndex = 0; channelIndex < 16; ++channelIndex)
        {
            auto cell =
                juce::Rectangle<int>(panel.getX() + static_cast<int>(channelIndex % 4) * cellWidth,
                                     panel.getY() + static_cast<int>(channelIndex / 4) * cellHeight,
                                     cellWidth - 6,
                                     cellHeight - 6);
            g.setColour(juce::Colour(0xff1d2b38));
            g.fillRoundedRectangle(cell.toFloat(), 5.0f);
            juce::String name;
            for (const auto& route : mimic::padRoutes)
            {
                const int global = static_cast<int>(adc * 16 + channelIndex);
                if (route.head == global)
                {
                    name = juce::String(route.name) + " H";
                }
                if (route.rim == global)
                {
                    name = juce::String(route.name) + " R";
                }
            }
            const auto value = meters[adc * 16 + channelIndex];
            g.setColour(muted);
            g.setFont(12.0f);
            g.drawText("CH " + juce::String(channelIndex) + "  " + name,
                       cell.reduced(5).removeFromTop(19),
                       juce::Justification::centredLeft);
            g.setColour(text);
            g.setFont(22.0f);
            g.drawText(juce::String(value),
                       cell.reduced(4).withTrimmedTop(20),
                       juce::Justification::centred);
            auto bar = cell.reduced(5).removeFromBottom(5).toFloat();
            bar.setWidth(bar.getWidth()
                         * juce::jlimit(0.0f, 1.0f, static_cast<float>(value) / 4095.0f));
            g.setColour(accent);
            g.fillRect(bar);
        }
    }
}
