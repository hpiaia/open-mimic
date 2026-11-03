// SPDX-License-Identifier: AGPL-3.0-only
#include "TriggerSettingsPanel.h"

#include "PadControl.h"

#include <algorithm>
#include <cmath>

namespace
{
const juce::Colour text{0xffedf3fa}, muted{0xff9aaec4}, accent{0xff4de2ba}, warning{0xfff2c14e};

float number(const juce::var& value, float fallback)
{
    return value.isVoid() ? fallback : static_cast<float>(static_cast<double>(value));
}

juce::String zoneName(int zone)
{
    return zone == 1 ? "Head" : zone == 2 ? "Rim" : zone == 3 ? "Rimshot" : "-";
}

// Log scale for ADC peaks: 1 count at the left, 4095 at the right.
float meterFraction(float counts)
{
    return juce::jlimit(0.0f, 1.0f, std::log10(juce::jmax(1.0f, counts)) / std::log10(4095.0f));
}
} // namespace

TriggerSettingsPanel::TriggerSettingsPanel()
{
    setOpaque(true);
    for (auto* component : std::initializer_list<juce::Component*>{&title,
                                                                   &subtitle,
                                                                   &detail,
                                                                   &modeLabel,
                                                                   &meterTitle,
                                                                   &measuredText,
                                                                   &close,
                                                                   &reset,
                                                                   &zoneMode})
    {
        addAndMakeVisible(*component);
    }
    for (auto* label : {&title, &subtitle, &detail, &modeLabel, &meterTitle, &measuredText})
    {
        label->setColour(juce::Label::textColourId, text);
    }
    title.setText("Trigger settings", juce::dontSendNotification);
    title.setFont(juce::FontOptions(29.0f, juce::Font::bold));
    subtitle.setColour(juce::Label::textColourId, muted);
    detail.setColour(juce::Label::textColourId, muted);
    detail.setJustificationType(juce::Justification::centred);
    measuredText.setColour(juce::Label::textColourId, muted);
    measuredText.setFont(juce::FontOptions(14.0f));
    measuredText.setJustificationType(juce::Justification::topLeft);
    meterTitle.setText("Last hit", juce::dontSendNotification);
    meterTitle.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    modeLabel.setText("Zone decision", juce::dontSendNotification);
    zoneMode.addItem("Rule + model", 1);
    zoneMode.addItem("Rule only", 2);
    zoneMode.addItem("Model first", 3);
    zoneMode.onChange = [this]
    {
        if (const auto* pad = selected())
        {
            run({"trigger", pad->id, "zone-mode", juce::String(zoneMode.getSelectedId() - 1)});
        }
    };

    struct Definition
    {
        const char* caption;
        const char* key;
        double minimum, maximum, step;
        bool automatic, rimOnly;
    };

    const std::array<Definition, 6> definitions{{
        {"Threshold", "threshold", 0.0, 200.0, 1.0, false, false},
        {"Mask time", "mask", 2.0, 50.0, 1.0, false, false},
        {"Retrigger cancel", "retrigger", 0.0, 100.0, 1.0, false, false},
        {"Rimshot threshold: head piezo", "rimshot-head", 0.0, 4095.0, 1.0, true, true},
        {"Rimshot threshold: rim piezo", "rimshot-rim", 0.0, 4095.0, 1.0, true, true},
        {"Head / rim balance", "balance", -100.0, 100.0, 1.0, false, true},
    }};
    for (std::size_t index = 0; index < controls.size(); ++index)
    {
        auto& control = controls[index];
        const auto& definition = definitions[index];
        control.key = definition.key;
        control.automatic = definition.automatic;
        control.rimOnly = definition.rimOnly;
        control.label.setText(definition.caption, juce::dontSendNotification);
        control.label.setColour(juce::Label::textColourId, text);
        control.label.setFont(juce::FontOptions(16.0f));
        auto& slider = control.slider;
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange(definition.minimum, definition.maximum, definition.step);
        if (definition.automatic)
        {
            slider.setSkewFactorFromMidPoint(300.0);
        }
        slider.setScrollWheelEnabled(false);
        slider.setColour(juce::Slider::trackColourId, accent);
        slider.setColour(juce::Slider::thumbColourId, accent);
        slider.onDragEnd = [this, &control] { send(control); };
        slider.onValueChange = [this, &control]
        { control.label.setText(valueText(control), juce::dontSendNotification); };
        addAndMakeVisible(control.label);
        addAndMakeVisible(slider);
    }
    close.onClick = [this]
    {
        if (!selectedId.isEmpty())
        {
            selectedId.clear();
            update();
            resized();
            return;
        }
        if (onClose)
        {
            onClose();
        }
    };
    reset.onClick = [this]
    {
        if (const auto* pad = selected())
        {
            run({"trigger-reset", pad->id});
        }
    };
    startTimerHz(5);
}

TriggerSettingsPanel::~TriggerSettingsPanel()
{
    stopTimer();
}

void TriggerSettingsPanel::show()
{
    selectedId.clear();
    commandError.clear();
    refresh();
    update();
    resized();
}

const TriggerSettingsPanel::Pad* TriggerSettingsPanel::selected() const
{
    const auto found =
        std::find_if(pads.begin(), pads.end(), [this](const Pad& p) { return p.id == selectedId; });
    return found == pads.end() ? nullptr : &*found;
}

juce::String TriggerSettingsPanel::valueText(const Control& control) const
{
    const auto value = control.slider.getValue();
    juce::String shown;
    if (control.key == "mask")
    {
        shown = juce::String(value, 0) + " ms";
    }
    else if (control.key == "balance")
    {
        shown = value == 0.0 ? juce::String("centre")
                             : (value > 0.0 ? "head +" : "rim ") + juce::String(std::abs(value), 0);
    }
    else if (control.automatic && value < 1.0)
    {
        const auto* pad = selected();
        const auto calibrated = pad == nullptr                  ? 0.0f
                                : control.key == "rimshot-head" ? pad->effectiveRimshotHead
                                                                : pad->effectiveRimshotRim;
        shown = "auto" + (pad && pad->measured ? " (" + juce::String(calibrated, 0) + ")" : "");
    }
    else
    {
        shown = juce::String(value, 0);
    }
    return control.label.getText().upToFirstOccurrenceOf(":", false, false).trim() + ":  " + shown;
}

void TriggerSettingsPanel::send(Control& control)
{
    const auto* pad = selected();
    if (pad == nullptr)
    {
        return;
    }
    auto value = control.slider.getValue();
    if (control.automatic && value < 2.0)
    {
        value = -1.0;
    }
    run({"trigger", pad->id, control.key, juce::String(value, 1)});
}

void TriggerSettingsPanel::choose(const juce::String& id)
{
    selectedId = id;
    commandError.clear();
    if (const auto* pad = selected())
    {
        for (auto& control : controls)
        {
            const auto value = control.key == "threshold"      ? pad->threshold
                               : control.key == "mask"         ? pad->maskMs
                               : control.key == "retrigger"    ? pad->retriggerCancel
                               : control.key == "rimshot-head" ? pad->rimshotHead
                               : control.key == "rimshot-rim"  ? pad->rimshotRim
                                                               : pad->balance;
            control.slider.setValue(control.automatic && value < 0.0f ? 0.0 : value,
                                    juce::dontSendNotification);
            control.label.setText(valueText(control), juce::dontSendNotification);
        }
        zoneMode.setSelectedId(pad->zoneMode + 1, juce::dontSendNotification);
    }
    update();
    resized();
}

void TriggerSettingsPanel::run(const juce::StringArray& args)
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

void TriggerSettingsPanel::timerCallback()
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

void TriggerSettingsPanel::refresh()
{
    const auto statusFile = juce::File(mimic::padStatusPath);
    if (!statusFile.existsAsFile() || statusFile.getSize() > 65536)
    {
        running = false;
        return;
    }
    const auto data = juce::JSON::parse(statusFile.loadFileAsString());
    running = static_cast<bool>(data.getProperty("running", false));
    error = data.getProperty("error", "").toString();
    engineActive = static_cast<bool>(data.getProperty("engine", {}).getProperty("active", false));
    std::vector<Pad> updated;
    if (const auto* values = data.getProperty("pads", {}).getArray())
    {
        for (const auto& p : *values)
        {
            Pad pad;
            pad.id = p["id"].toString();
            pad.name = p["name"].toString();
            pad.zones = static_cast<int>(p["zoneCount"]);
            pad.head = static_cast<int>(p["head"]);
            const auto trigger = p["trigger"];
            pad.threshold = number(trigger["threshold"], 0.0f);
            pad.maskMs = number(trigger["maskMs"], 8.0f);
            pad.retriggerCancel = number(trigger["retriggerCancel"], 30.0f);
            pad.balance = number(trigger["balance"], 0.0f);
            pad.rimshotHead = number(trigger["rimshotHead"], -1.0f);
            pad.rimshotRim = number(trigger["rimshotRim"], -1.0f);
            pad.zoneMode = static_cast<int>(trigger["zoneMode"]);
            const auto measured = trigger["measured"];
            pad.measured = static_cast<bool>(measured["calibrated"]);
            pad.measuredRimshotHead = number(measured["rimshotHead"], 0.0f);
            pad.measuredRimshotRim = number(measured["rimshotRim"], 0.0f);
            const auto effective = trigger["effective"];
            pad.headCeiling = number(effective["headRatioCeiling"], 0.0f);
            pad.rimFloor = number(effective["rimRatioFloor"], 0.0f);
            pad.effectiveRimshotHead = number(effective["rimshotHead"], 0.0f);
            pad.effectiveRimshotRim = number(effective["rimshotRim"], 0.0f);
            const auto hit = trigger["lastHit"];
            pad.hits = static_cast<juce::int64>(hit["count"]);
            pad.lastHeadPeak = number(hit["headPeak"], 0.0f);
            pad.lastRimPeak = number(hit["rimPeak"], 0.0f);
            pad.lastZone = static_cast<int>(hit["zone"]);
            pad.lastRuleZone = static_cast<int>(hit["ruleZone"]);
            pad.lastVelocity = static_cast<int>(hit["velocity"]);
            pad.lastModelDecided = static_cast<bool>(hit["modelDecided"]);
            pad.modelActive = p["model"]["state"].toString() == "active";
            updated.push_back(pad);
        }
    }
    const bool rebuild = pads.size() != updated.size();
    pads = std::move(updated);
    if (rebuild)
    {
        rebuildButtons();
    }
    // Keep sliders in step with the service unless the player is dragging one.
    if (const auto* pad = selected())
    {
        for (auto& control : controls)
        {
            if (control.slider.isMouseButtonDown() || commandBusy)
            {
                continue;
            }
            const auto value = control.key == "threshold"      ? pad->threshold
                               : control.key == "mask"         ? pad->maskMs
                               : control.key == "retrigger"    ? pad->retriggerCancel
                               : control.key == "rimshot-head" ? pad->rimshotHead
                               : control.key == "rimshot-rim"  ? pad->rimshotRim
                                                               : pad->balance;
            const auto shown = control.automatic && value < 0.0f ? 0.0 : value;
            if (std::abs(control.slider.getValue() - shown) > 0.01)
            {
                control.slider.setValue(shown, juce::dontSendNotification);
            }
            control.label.setText(valueText(control), juce::dontSendNotification);
        }
        if (!commandBusy && zoneMode.getSelectedId() != pad->zoneMode + 1)
        {
            zoneMode.setSelectedId(pad->zoneMode + 1, juce::dontSendNotification);
        }
    }
}

void TriggerSettingsPanel::rebuildButtons()
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

void TriggerSettingsPanel::update()
{
    const auto* pad = selected();
    const bool grid = pad == nullptr;
    subtitle.setText(grid ? "Choose a pad"
                          : pad->name
                                + (pad->measured ? "  /  calibrated head-rim separation"
                                                 : "  /  not calibrated: generic separation"),
                     juce::dontSendNotification);
    close.setButtonText(grid ? "Close" : "Back");
    for (std::size_t i = 0; i < padButtons.size(); ++i)
    {
        padButtons[i]->setVisible(grid);
        padButtons[i]->setEnabled(running && pads[i].head >= 0);
    }
    for (auto& control : controls)
    {
        const bool shown = !grid && (!control.rimOnly || pad->zones == 2);
        control.label.setVisible(shown);
        control.slider.setVisible(shown);
        control.slider.setEnabled(running);
    }
    const bool dual = !grid && pad->zones == 2;
    modeLabel.setVisible(dual);
    zoneMode.setVisible(dual);
    zoneMode.setEnabled(running && !commandBusy);
    reset.setVisible(!grid);
    reset.setEnabled(running && !commandBusy);
    meterTitle.setVisible(!grid);
    measuredText.setVisible(!grid);
    if (!grid)
    {
        juce::String measured;
        if (pad->zones == 2)
        {
            measured << "Head band: rim/head ratio below " << juce::String(pad->headCeiling, 2)
                     << "\nRim band: ratio above " << juce::String(pad->rimFloor, 2)
                     << "\nRimshot when the other piezo exceeds "
                     << juce::String(pad->effectiveRimshotHead, 0) << " (head) or "
                     << juce::String(pad->effectiveRimshotRim, 0) << " (rim)"
                     << "\nClassifier: " << (pad->modelActive ? "pad model" : "shared model");
        }
        else
        {
            measured << "Single-zone pad: every hit is a head hit.";
        }
        measuredText.setText(measured, juce::dontSendNotification);
    }
    juce::String footer = grid ? "Settings apply immediately and persist on the module."
                               : "Hit the pad to see its peaks. Yellow lines are rimshot levels.";
    if (!engineActive && !grid)
    {
        footer = "Live MIDI is off: start it from Pad MIDI map to see hits here.";
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

void TriggerSettingsPanel::resized()
{
    auto area = getLocalBounds().reduced(20, 12);
    auto header = area.removeFromTop(46);
    close.setBounds(header.removeFromRight(112));
    if (!selectedId.isEmpty())
    {
        reset.setBounds(header.removeFromRight(220).reduced(6, 0));
    }
    title.setBounds(header);
    subtitle.setBounds(area.removeFromTop(30));
    detail.setBounds(area.removeFromBottom(40));
    area.removeFromTop(8);
    const int width = (area.getWidth() - 24) / 3;
    for (std::size_t i = 0; i < padButtons.size(); ++i)
    {
        padButtons[i]->setBounds(area.getX() + static_cast<int>(i % 3) * (width + 12),
                                 area.getY() + static_cast<int>(i / 3) * 82,
                                 width,
                                 70);
    }
    auto left = area.removeFromLeft(area.getWidth() * 3 / 5);
    area.removeFromLeft(24);
    meterBounds = area;
    const int rows = 6;
    const int rowHeight = juce::jmin(62, left.getHeight() / (rows + 1));
    for (auto& control : controls)
    {
        if (!control.label.isVisible())
        {
            continue;
        }
        auto row = left.removeFromTop(rowHeight);
        control.label.setBounds(row.removeFromTop(22));
        control.slider.setBounds(row.reduced(0, 2));
    }
    if (zoneMode.isVisible())
    {
        auto row = left.removeFromTop(rowHeight);
        modeLabel.setBounds(row.removeFromLeft(150));
        zoneMode.setBounds(row.reduced(0, 6));
    }
    meterTitle.setBounds(meterBounds.removeFromTop(28));
    measuredText.setBounds(meterBounds.removeFromBottom(92));
}

void TriggerSettingsPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff101820));
    const auto* pad = selected();
    if (pad == nullptr)
    {
        return;
    }
    auto panel = meterBounds.reduced(0, 6);
    g.setColour(juce::Colour(0xff1d2b38));
    g.fillRoundedRectangle(panel.toFloat(), 8.0f);
    panel = panel.reduced(14, 12);
    const auto drawBar = [&](juce::Rectangle<int> row, const char* caption, float peak, float line)
    {
        g.setColour(muted);
        g.setFont(14.0f);
        g.drawText(juce::String(caption) + "  " + juce::String(peak, 0),
                   row.removeFromTop(18),
                   juce::Justification::centredLeft);
        auto bar = row.removeFromTop(22).toFloat();
        g.setColour(juce::Colour(0xff101820));
        g.fillRoundedRectangle(bar, 4.0f);
        auto filled = bar;
        filled.setWidth(bar.getWidth() * meterFraction(peak));
        g.setColour(accent);
        g.fillRoundedRectangle(filled, 4.0f);
        if (line > 0.0f && line < 4095.0f)
        {
            const auto x = bar.getX() + bar.getWidth() * meterFraction(line);
            g.setColour(warning);
            g.fillRect(x - 1.5f, bar.getY() - 4.0f, 3.0f, bar.getHeight() + 8.0f);
        }
    };
    auto head = panel.removeFromTop(52);
    drawBar(
        head, "Head piezo", pad->lastHeadPeak, pad->zones == 2 ? pad->effectiveRimshotHead : 0.0f);
    if (pad->zones == 2)
    {
        panel.removeFromTop(8);
        drawBar(panel.removeFromTop(52), "Rim piezo", pad->lastRimPeak, pad->effectiveRimshotRim);
    }
    panel.removeFromTop(12);
    g.setColour(text);
    g.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    const auto decision =
        pad->hits == 0 ? juce::String("No hit yet")
                       : zoneName(pad->lastZone) + "   velocity " + juce::String(pad->lastVelocity);
    g.drawText(decision, panel.removeFromTop(32), juce::Justification::centredLeft);
    g.setColour(muted);
    g.setFont(14.0f);
    if (pad->hits > 0 && pad->zones == 2)
    {
        const auto ratio = pad->lastRimPeak / juce::jmax(1.0f, pad->lastHeadPeak);
        g.drawText("ratio " + juce::String(ratio, 2) + "   rule: " + zoneName(pad->lastRuleZone)
                       + (pad->lastModelDecided ? "   model decided" : "   rule decided")
                       + "   hits " + juce::String(pad->hits),
                   panel.removeFromTop(22),
                   juce::Justification::centredLeft);
    }
}
