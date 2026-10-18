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
#include "UI/Widgets.h"
#include "Video/DeckVideo.h"

namespace midj
{
// Ventana principal (diseno "Escenario"): arriba una barra con el nombre, el formato del video,
// el sampler (compacto) y a la derecha las pestanas AUDIO / PROGRAMA junto a Proyector, MIDI,
// Atajos y Ajustes; debajo los dos decks a los lados y en el centro el programa (o el EQ) con
// el volumen de cada deck a los lados y la franja del crossfader y las transiciones; abajo, con
// el ancho que se quiera (barritas para arrastrar), la biblioteca, el automix y las escenas y
// fuentes. La fila de los decks mide lo que piden los decks: lo que se oculta en ellos desde
// Ajustes lo ganan los paneles de abajo. Atajos de teclado: ver shortcutsHelp().
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

    // Paneles y partes de los decks que se pueden ocultar desde Ajustes, y los anchos de abajo.
    struct PanelSettings
    {
        bool library = true, automix = true, scenes = true, sampler = true;
        DeckComponent::Sections deck;
        int automixWidth = defaultAutomixWidth, scenesWidth = defaultScenesWidth;
        static constexpr int defaultAutomixWidth = 300, defaultScenesWidth = 280, minWidth = 220, maxWidth = 640;
    };
    PanelSettings& getPanels() { return panels; }
    void panelsChanged();

private:
    // Carga usando el analisis guardado en la biblioteca si lo hay (carga mucho mas rapida).
    bool loadIntoDeck (Deck& deck, const juce::File& file);
    // Doble clic en el navegador: al primer deck que no este sonando, empezando por A.
    void loadIntoFreeDeck (const juce::File& file);
    void toggleMidiLearn();
    void showSettings();
    void saveSamplerBank();
    static juce::File getDataFolder() { return Library::getDefaultFile().getParentDirectory(); }
    void timerCallback() override;
    // Modo de prueba sin controladora: --simular-midi archivo (ver README).
    void startMidiSimulation (const juce::File& script);
    // Portada de un deck: un fotograma del video, leido en segundo plano.
    void requestPoster (int deckIndex, const juce::File& file);

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
    int activeDeck = 0;
    std::array<bool, Sampler::numPads> padKeyDown {};

    // Barra de arriba.
    juce::TextButton projectorButton { "Proyector" }, midiButton { "MIDI" }, helpButton { "Atajos" };
    juce::TextButton audioTabButton { "AUDIO" }, programTabButton { "PROGRAMA" };
    IconButton settingsButton { Icons::Glyph::sliders, "Ajustes: que se ve en pantalla y anchos" };
    juce::Rectangle<int> headerArea, tabsArea;
    void updateTabButtons (bool program);

    // Abajo: biblioteca | automix | escenas y fuentes, con barritas para cambiar el ancho.
    PanelSettings panels;
    ResizeHandle automixHandle, scenesHandle;
    int dragStartWidth = 0;

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
