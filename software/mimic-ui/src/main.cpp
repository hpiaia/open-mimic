// SPDX-License-Identifier: AGPL-3.0-only
#include "MainComponent.h"

class MimicApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override
    {
        return "Open Mimic";
    }

    const juce::String getApplicationVersion() override
    {
        return "0.2.0";
    }

    bool moreThanOneInstanceAllowed() override
    {
        return false;
    }

    void initialise(const juce::String&) override
    {
        window = std::make_unique<Window>();
    }

    void shutdown() override
    {
        window.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override {}

private:
    class Window final : public juce::DocumentWindow
    {
    public:
        Window()
            : DocumentWindow("Open Mimic", juce::Colour(0xff101720), DocumentWindow::closeButton)
        {
            const bool instrumentDisplay =
                juce::SystemStats::getEnvironmentVariable("OPEN_MIMIC_FULLSCREEN", "") == "1";
            setUsingNativeTitleBar(!instrumentDisplay);
            setContentOwned(new MainComponent(), true);
            setResizable(!instrumentDisplay, false);
            setResizeLimits(800, 540, 4096, 2160);
            centreWithSize(1024, 600);
            if (instrumentDisplay)
            {
                setTitleBarHeight(0);
                setFullScreen(true);
            }
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(MimicApplication)
