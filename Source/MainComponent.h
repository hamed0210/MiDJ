#pragma once

#include "Control/ControlSurface.h"
#include "Control/MidiController.h"
#include "Engine/Deck.h"
#include "Engine/Mixer.h"
#include "Engine/StemsService.h"
#include "Library/Library.h"
#include "UI/AutomixPanel.h"
#include "UI/DarkLookAndFeel.h"
#include "UI/DeckComponent.h"
#include "UI/LibraryBrowser.h"
#include "UI/MidiLearnOverlay.h"
#include "UI/MixerComponent.h"
#include "UI/SamplerPanel.h"
#include "UI/ScenePanel.h"
#include "UI/VideoWindow.h"
#include "Video/DeckVideo.h"

namespace midj
{
// Ventana principal, como VirtualDJ: arriba los dos decks con el mezclador en el centro (con
// sus pestanas AUDIO y VIDEO y el crossfader), en medio el sampler y abajo, ajustables a lo
// ancho, la biblioteca, el automix y las escenas y fuentes del video. Atajos de teclado (ver
// shortcutsHelp()).
class MainComponent : public juce::AudioAppComponent,
                      public juce::DragAndDropContainer,
                      private juce::KeyListener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;
    void releaseResources() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void parentHierarchyChanged() override;

    static juce::String shortcutsHelp();

private:
    // Carga usando el analisis guardado en la biblioteca si lo hay (carga mucho mas rapida).
    bool loadIntoDeck (Deck& deck, const juce::File& file);
    // Doble clic en el navegador: al primer deck que no este sonando, empezando por A.
    void loadIntoFreeDeck (const juce::File& file);
    void toggleMidiLearn();
    void saveSamplerBank();
    static juce::File getDataFolder() { return Library::getDefaultFile().getParentDirectory(); }
    void timerCallback() override;
    // Modo de prueba sin controladora: --simular-midi archivo (ver README).
    void startMidiSimulation (const juce::File& script);

    // Atajos de teclado (llegan aqui si el control con el foco no los usa).
    bool keyPressed (const juce::KeyPress& key, juce::Component* origin) override;
    bool keyStateChanged (bool isKeyDown, juce::Component* origin) override;
    void setActiveDeck (int deckIndex);
    DeckComponent& activeView() { return activeDeck == 0 ? deckViewA : deckViewB; }
    // Los botones, mandos y listas no se quedan con el foco del teclado al hacer clic (si no,
    // el espacio pulsaria el ultimo boton): solo los textos, las listas y la vista previa.
    void keepFocusForShortcuts (juce::Component& root);
    void saveLayout();
    void loadLayout();

    DarkLookAndFeel lookAndFeel;
    juce::AudioFormatManager formatManager;
    Deck deckA { formatManager };
    Deck deckB { formatManager };
    Mixer mixer { deckA, deckB };
    Library library { formatManager };
    Automix automix { deckA, deckB, mixer };
    StemsService stems { formatManager, getDataFolder(),
                         juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("models").getChildFile ("spleeter-2stems") };
    std::array<juce::File, 2> lastLoaded;   // para leer de la cache los stems de cada cancion nueva
    ControlSurface controlSurface { deckA, deckB, mixer };
    MidiController midi { controlSurface };

    DeckComponent deckViewA { deckA, deckB, mixer.getEffects (0), "A" };
    DeckComponent deckViewB { deckB, deckA, mixer.getEffects (1), "B" };
    MixerComponent mixerView { mixer };
    std::unique_ptr<LibraryBrowser> browser;   // se crea con la biblioteca ya abierta
    AutomixPanel automixPanel { automix, library };
    SamplerPanel samplerPanel { mixer.getSampler(), formatManager };
    juce::TooltipWindow tooltips { this };
    juce::TextButton midiButton { "MIDI" }, helpButton { "Atajos" };
    int activeDeck = 0;
    std::array<bool, Sampler::numPads> padKeyDown {};

    // Abajo: biblioteca | automix | escenas y fuentes, con divisores que se arrastran.
    juce::StretchableLayoutManager bottomLayout;
    juce::StretchableLayoutResizerBar bottomBar1 { &bottomLayout, 1, true }, bottomBar2 { &bottomLayout, 3, true };
    juce::Component videoColumn;   // solo para colocar las escenas y fuentes

    // Video: un decodificador por deck y el escenario (compositor, escenas, vista previa).
    // %APPDATA%MiDJvideo.xml: <VIDEO device="cpu" width="1280" height="720" fps="30"/>
    // (device="cpu" decodifica sin la tarjeta grafica; los demas, el tamano y ritmo del programa).
    static bool videoGpuAllowed();
    static Compositor::Settings videoSettings();
    DeckVideo videoA { deckA, videoSettings().width, videoGpuAllowed() };
    DeckVideo videoB { deckB, videoSettings().width, videoGpuAllowed() };
    std::unique_ptr<VideoStage> videoStage;
    std::unique_ptr<ScenesPanel> scenesPanel;
    std::unique_ptr<SourcesPanel> sourcesPanel;
    juce::String videoTitleFor (int deckIndex);
    std::array<std::pair<juce::File, juce::String>, 2> videoTitles;   // para no consultar la biblioteca a cada fotograma
    MidiLearnOverlay midiOverlay { midi };
    int timerTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
}
