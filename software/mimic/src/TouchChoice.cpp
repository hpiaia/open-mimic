// SPDX-License-Identifier: AGPL-3.0-only
#include "TouchChoice.h"

#include <algorithm>
#include <functional>

namespace
{
constexpr int rowHeight = 60;
const juce::Colour background{0xff101720};
const juce::Colour accent{0xff4de2ba};
} // namespace

class TouchChoice::Popup final : public juce::Component
{
public:
    explicit Popup(TouchChoice& choice) : owner(choice), content(*this)
    {
        setOpaque(true);
        addAndMakeVisible(title);
        addAndMakeVisible(close);
        addAndMakeVisible(viewport);
        title.setText("Select an option", juce::dontSendNotification);
        title.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colours::white);
        close.onClick = [this] { setVisible(false); };
        viewport.setViewedComponent(&content, false);
        // evdev may expose the touchscreen as an emulated mouse source.
        viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::all);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(18);
        setWantsKeyboardFocus(true);
    }

    ~Popup() override
    {
        viewport.setViewedComponent(nullptr, false);
    }

    void show()
    {
        setVisible(true);
        toFront(true);
        grabKeyboardFocus();
        const auto selected =
            std::find_if(owner.items.begin(),
                         owner.items.end(),
                         [this](const Item& item) { return item.identifier == owner.selectedId; });
        const auto index = static_cast<int>(std::distance(owner.items.begin(), selected));
        viewport.setViewPosition(0, juce::jmax(0, index * rowHeight - rowHeight));
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(background);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(36, 20);
        auto header = area.removeFromTop(60);
        close.setBounds(header.removeFromRight(120));
        title.setBounds(header);
        area.removeFromTop(12);
        viewport.setBounds(area);
        content.setSize(juce::jmax(1, area.getWidth() - 18),
                        static_cast<int>(owner.items.size()) * rowHeight);
    }

    void parentSizeChanged() override
    {
        if (auto* parent = getParentComponent())
        {
            setBounds(parent->getLocalBounds());
        }
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            setVisible(false);
            return true;
        }
        return false;
    }

private:
    class Content final : public juce::Component
    {
    public:
        explicit Content(Popup& popup) : owner(popup) {}

        void paint(juce::Graphics& graphics) override
        {
            graphics.setFont(juce::FontOptions(22.0f));
            for (std::size_t index = 0; index < owner.owner.items.size(); ++index)
            {
                const auto& item = owner.owner.items[index];
                const juce::Rectangle<int> bounds(
                    0, static_cast<int>(index) * rowHeight, getWidth(), rowHeight);
                const bool selected = item.identifier == owner.owner.selectedId;
                graphics.setColour(selected ? juce::Colour(0xff243c40) : juce::Colour(0xff1c2836));
                graphics.fillRoundedRectangle(bounds.reduced(0, 3).toFloat(), 6.0f);
                graphics.setColour(selected ? accent : juce::Colours::white);
                graphics.drawText(
                    item.text, bounds.reduced(18, 0), juce::Justification::centredLeft);
            }
        }

        void mouseDown(const juce::MouseEvent& event) override
        {
            pressPosition = event.getScreenPosition();
            dragged = false;
        }

        void mouseDrag(const juce::MouseEvent& event) override
        {
            // Compare screen coordinates: scrolling moves the content under the finger.
            dragged = dragged || pressPosition.getDistanceFrom(event.getScreenPosition()) > 8;
        }

        void mouseUp(const juce::MouseEvent& event) override
        {
            if (dragged || pressPosition.getDistanceFrom(event.getScreenPosition()) > 8
                || !getLocalBounds().contains(event.getPosition()))
            {
                return;
            }
            const auto index = static_cast<std::size_t>(event.y / rowHeight);
            if (index < owner.owner.items.size())
            {
                owner.owner.setSelectedId(owner.owner.items[index].identifier,
                                          juce::dontSendNotification);
                // Dismiss immediately in this window, without a native popup event loop.
                owner.setVisible(false);
            }
        }

    private:
        Popup& owner;
        juce::Point<int> pressPosition;
        bool dragged = false;
    };

    TouchChoice& owner;
    Content content;
    juce::Viewport viewport;
    juce::Label title;
    juce::TextButton close{"Cancel"};
};

TouchChoice::TouchChoice()
{
    addAndMakeVisible(button);
    button.onClick = [this] { showChoices(); };
}

TouchChoice::~TouchChoice() = default;

void TouchChoice::addItem(const juce::String& text, int identifier)
{
    jassert(identifier != 0);
    items.push_back({text, identifier});
}

void TouchChoice::setSelectedId(int identifier, juce::NotificationType)
{
    const auto item = std::find_if(items.begin(),
                                   items.end(),
                                   [identifier](const Item& candidate)
                                   { return candidate.identifier == identifier; });
    if (item != items.end())
    {
        selectedId = identifier;
        button.setButtonText(item->text + "  v");
    }
}

int TouchChoice::getSelectedId() const noexcept
{
    return selectedId;
}

void TouchChoice::resized()
{
    button.setBounds(getLocalBounds());
}

void TouchChoice::showChoices()
{
    if (auto* parent = getTopLevelComponent(); parent != this)
    {
        if (popup == nullptr)
        {
            popup = std::make_unique<Popup>(*this);
        }
        // Attach above the application content so the selector covers its header too.
        if (popup->getParentComponent() != parent)
        {
            parent->addChildComponent(*popup);
        }
        popup->setBounds(parent->getLocalBounds());
        popup->show();
    }
}
