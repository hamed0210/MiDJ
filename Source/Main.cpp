#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

class MiDJApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "MiDJ"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>();
    }

    void shutdown() override { mainWindow = nullptr; }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow()
            : DocumentWindow ("MiDJ", juce::Colour (0xff15171c), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new midj::MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (900, 500, 4000, 3000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (MiDJApplication)
