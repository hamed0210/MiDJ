#pragma once

#include "Engine/Deck.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace midj
{
// Panel de un deck: titulo, BPM, tiempo, forma de onda con rejilla de pulsos,
// botones CUE / Play / SYNC, bucles de 1-2-4-8 pulsos, cargar, fader de tempo y KEY.
class DeckComponent : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    // 'other' es el deck contrario: SYNC se sincroniza con el.
    DeckComponent (Deck& deck, Deck& other, const juce::String& name);
    ~DeckComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;   // clic en la forma de onda = saltar

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void chooseFile();
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
    juce::TextButton keyButton { "KEY" };   // keylock: tempo sin cambiar el tono
    static constexpr std::array<int, 4> loopSizes { 1, 2, 4, 8 };
    std::array<juce::TextButton, 4> loopButtons;
    juce::Label loopLabel;
    juce::Slider tempoSlider;
    juce::Label titleLabel, bpmLabel, timeLabel, tempoLabel;
    juce::Rectangle<int> waveformArea;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeckComponent)
};
}
