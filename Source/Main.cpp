#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

class MiDJApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "MiDJ"; }
    const juce::String getApplicationVersion() override { return "0.3.2"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        // Textos de JUCE en espanol (ajustes de la tarjeta de sonido y dialogos).
        juce::LocalisedStrings::setCurrentMappings (new juce::LocalisedStrings (juce::String::fromUTF8 (
            "language: Spanish\n"
            "\"Audio device type:\" = \"Tipo de audio:\"\n"
            "\"Output:\" = \"Salida:\"\n"
            "\"Input:\" = \"Entrada:\"\n"
            "\"Sample rate:\" = \"Frecuencia:\"\n"
            "\"Audio buffer size:\" = \"Tamaño del bloque:\"\n"
            "\"samples\" = \"muestras\"\n"
            "\"Test\" = \"Probar\"\n"
            "\"Active output channels:\" = \"Salidas activas:\"\n"
            "\"Active input channels:\" = \"Entradas activas:\"\n"
            "\"Control Panel\" = \"Panel de control\"\n"
            "\"Reset Device\" = \"Reiniciar\"\n"
            "\"Show advanced settings...\" = \"Ver más ajustes...\"\n"
            "\"Hide advanced settings\" = \"Ocultar ajustes\"\n"
            "\"<< none >>\" = \"<< ninguna >>\"\n"
            "\"(no device)\" = \"(sin tarjeta)\"\n"
            "\"OK\" = \"Aceptar\"\n"
            "\"Cancel\" = \"Cancelar\"\n"), false));
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
            // Tamano, posicion y maximizada como se dejo la ultima vez (si no, centrada).
            const auto saved = stateFile().loadFileAsString().trim();
            if (saved.isEmpty() || ! restoreWindowStateFromString (saved))
                centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        ~MainWindow() override { saveState(); }

        void closeButtonPressed() override
        {
            saveState();
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        static juce::File stateFile() { return midj::Library::getDefaultFile().getSiblingFile ("window.txt"); }
        void saveState() { if (isShowing()) stateFile().replaceWithText (getWindowStateAsString()); }

    public:

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (MiDJApplication)
