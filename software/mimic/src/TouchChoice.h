// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

// A full-window choice list with finger-sized rows and drag scrolling.
class TouchChoice final : public juce::Component
{
public:
    TouchChoice();
    ~TouchChoice() override;

    void addItem(const juce::String& text, int identifier);
    void setSelectedId(int identifier, juce::NotificationType notification);
    [[nodiscard]] int getSelectedId() const noexcept;
    void resized() override;

private:
    struct Item
    {
        juce::String text;
        int identifier;
    };
    class Popup;
    void showChoices();

    std::vector<Item> items;
    int selectedId = 0;
    juce::TextButton button;
    std::unique_ptr<Popup> popup;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TouchChoice)
};
