#pragma once

#include "Engine/Deck.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// Panel de un deck: titulo, BPM, tiempo, forma de onda, play, cargar y tempo.
class DeckComponent : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    DeckComponent (Deck& deck, const juce::String& name);
    ~DeckComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;   // clic en la forma de onda = saltar

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void chooseFile();
    void updateLabels();
    juce::Colour accent() const;

    Deck& deck;
    juce::String deckName;

    juce::TextButton loadButton { "Cargar" };
    juce::TextButton playButton { "Play" };
    juce::Slider tempoSlider;
    juce::Label titleLabel, bpmLabel, timeLabel, tempoLabel;
    juce::Rectangle<int> waveformArea;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeckComponent)
};
}
