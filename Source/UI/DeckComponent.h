#pragma once

#include "Engine/Deck.h"
#include "UI/EffectsRow.h"
#include "UI/Icons.h"
#include "UI/Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace midj
{
// Tarjeta de un deck (diseno "Escenario"), de arriba abajo:
//  - DECK A / B, su estado (sonando, en pausa...) y el boton de abrir un archivo;
//  - la portada (un fotograma si es un video) con titulo, artista, BPM y tiempo;
//  - la forma de onda, delgada (lo ya sonado mas apagado) con la rejilla de pulsos, el CUE y el bucle;
//  - el pitch +-16 % con su centro marcado (verde en 0, se engancha al pasar) y KEY;
//  - bucles de 1-2-4-8 pulsos y stems (voz / instrumental);
//  - tres efectos;
//  - Play / Pausa, CUE y SYNC.
// Desde Ajustes se ocultan la onda, el pitch, los bucles, los stems o los efectos: lo que queda se
// junta sin huecos y getPreferredHeight() da el alto justo (si la ventana da mas, crece la portada).
class DeckComponent : public juce::Component,
                      public juce::DragAndDropTarget,
                      public juce::FileDragAndDropTarget,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    // other es el deck contrario: SYNC se sincroniza con el. effects: los efectos de este deck.
    DeckComponent (Deck& deck, Deck& other, EffectRack& effects, const juce::String& name);
    ~DeckComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;   // clic en la forma de onda = saltar

    // Partes que se ven (Ajustes).
    struct Sections
    {
        bool waveform = true, pitch = true, loops = true, stems = true, effects = true;
    };
    void setSections (const Sections& s);
    int getPreferredHeight() const;

    // Stems: estado de la separacion de la cancion cargada (lo da la ventana) y pedirla.
    enum class StemsState { none, queued, working, failed };
    std::function<std::pair<StemsState, double>()> getStemsState;
    std::function<void()> onStemsRequested;

    // Titulo y artista de un archivo (de la biblioteca); sin esto, el nombre que da el deck.
    std::function<std::pair<juce::String, juce::String> (const juce::File&)> describe;
    // Portada: un fotograma del video cargado (o nada si es solo audio).
    void setPoster (const juce::File& forFile, const juce::Image& image);

    // Controles asignables por MIDI. prefix: "deck1." o "deck2.".
    void addLearnTargets (std::vector<LearnTarget>& out, const juce::String& prefix);

    // Cargar un archivo: lo usan el boton de abrir y el arrastre. Si no se asigna, deck.load().
    std::function<void (const juce::File&)> onLoadFile;

    // Deck activo: el de los atajos de teclado (Espacio, C, S...). Se marca con su color.
    std::function<void()> onActivated;
    void setActive (bool isActive);
    // Atajos: los mismos botones de la pantalla.
    void togglePlay() { playButton.triggerClick(); }
    void pressCue() { cueButton.triggerClick(); }
    void pressSync() { syncButton.triggerClick(); }
    void toggleKeyLock() { keyButton.triggerClick(); }

    // Arrastre desde el navegador de la biblioteca y desde el Explorador de Windows.
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails&) override { setDragHighlight (true); }
    void itemDragExit (const SourceDetails&) override  { setDragHighlight (false); }
    void itemDropped (const SourceDetails& details) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { setDragHighlight (true); }
    void fileDragExit (const juce::StringArray&) override           { setDragHighlight (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void chooseFile();
    void loadFile (const juce::File& file);
    void setDragHighlight (bool on) { dragHighlight = on; repaint(); }
    void updateLabels();
    void paintWaveform (juce::Graphics& g);
    void renderWaveCache (float scale);
    void paintPoster (juce::Graphics& g);
    float timeToX (double seconds) const;
    juce::Colour accent() const;
    void styleToggle (juce::TextButton& b, bool on, bool filled = true);

    Deck& deck;
    Deck& otherDeck;
    juce::String deckName;

    IconButton loadButton { Icons::Glyph::folder, "Abrir un archivo en este deck" };
    juce::TextButton cueButton { "Cue" }, playButton { "Play" }, syncButton { "Sync" }, keyButton { "KEY" };
    juce::TextButton stemsButton { "Separar stems" }, vocalButton { "Voz" }, instrumentalButton { "Instrumental" };
    static constexpr std::array<int, 4> loopSizes { 1, 2, 4, 8 };
    std::array<juce::TextButton, 4> loopButtons;
    CentredSlider tempoSlider { -16.0, 16.0, 0.35 };
    EffectsRow effectsRow;

    // Textos que se pintan (no son Labels: asi no roban clics ni el foco).
    juce::String title, artist, stateText, bpmText, timeText, pitchText;
    // Titulo que no cabe: se desplaza (pausa al principio de cada vuelta). Lo pone paint().
    juce::Rectangle<int> titleArea;
    bool titleScrolls = false;
    juce::String scrolledTitle;
    juce::uint32 titleScrollStart = 0;
    void paintTitle (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area);
    juce::File describedFile;
    juce::File posterFile;
    juce::Image poster;
    juce::Image waveBright, waveDim;   // la onda ya dibujada (ver renderWaveCache)
    bool waveDirty = true;
    int waveCacheSize = 0;
    float waveCacheScale = 0.0f;
    juce::Rectangle<int> headerArea, posterArea, infoArea, waveformArea, pitchLabelArea, pitchScaleArea, loopLabelArea;
    Sections sections;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragHighlight = false, active = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeckComponent)
};
}
