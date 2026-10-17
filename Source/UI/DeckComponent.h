#pragma once

#include "Engine/Deck.h"
#include "UI/EffectsRow.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace midj
{
// Panel de un deck: titulo, BPM, tiempo, forma de onda con rejilla de pulsos,
// botones CUE / Play / SYNC, efectos, bucles de 1-2-4-8 pulsos, cargar, fader de tempo y KEY.
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

    // Stems: estado de la separacion de la cancion cargada (lo da la ventana) y pedirla.
    enum class StemsState { none, queued, working, failed };
    std::function<std::pair<StemsState, double>()> getStemsState;
    std::function<void()> onStemsRequested;

    // Controles asignables por MIDI. prefix: "deck1." o "deck2.".
    void addLearnTargets (std::vector<LearnTarget>& out, const juce::String& prefix);

    // Cargar un archivo: lo usan el boton Cargar y el arrastre. Si no se asigna, deck.load().
    std::function<void (const juce::File&)> onLoadFile;

    // Deck activo: el de los atajos de teclado (Espacio, C, S...). Se marca con un borde fino.
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
    void paintBeatGrid (juce::Graphics& g);
    float timeToX (double seconds) const;
    juce::Colour accent() const;

    Deck& deck;
    Deck& otherDeck;
    juce::String deckName;

    juce::TextButton loadButton { "Cargar" };
    juce::TextButton cueButton { "CUE" };
    juce::TextButton playButton { "Play" };
    juce::TextButton syncButton { "SYNC" };
    juce::TextButton keyButton { "KEY" };
    juce::TextButton stemsButton { "STEMS" }, vocalButton { "VOZ" }, instrumentalButton { "INSTR" };   // keylock: tempo sin cambiar el tono
    static constexpr std::array<int, 4> loopSizes { 1, 2, 4, 8 };
    std::array<juce::TextButton, 4> loopButtons;
    juce::Label loopLabel;
    juce::Slider tempoSlider;
    juce::Label titleLabel, bpmLabel, timeLabel, tempoLabel;
    juce::Rectangle<int> waveformArea;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragHighlight = false, active = false;
    EffectsRow effectsRow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeckComponent)
};
}
