#pragma once

#include "Engine/Deck.h"
#include "Engine/Mixer.h"
#include "UI/DarkLookAndFeel.h"
#include "UI/DeckComponent.h"
#include "UI/MixerComponent.h"

namespace midj
{
// Ventana principal: dos decks, el mezclador en el centro, crossfader y el motor de audio.
class MainComponent : public juce::AudioAppComponent
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;
    void releaseResources() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    DarkLookAndFeel lookAndFeel;
    juce::AudioFormatManager formatManager;
    Deck deckA { formatManager };
    Deck deckB { formatManager };
    Mixer mixer { deckA, deckB };

    DeckComponent deckViewA { deckA, deckB, "A" };
    DeckComponent deckViewB { deckB, deckA, "B" };
    MixerComponent mixerView { mixer };
    juce::Slider crossfader;
    juce::Label crossfaderLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
}
