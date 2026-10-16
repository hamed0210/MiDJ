#pragma once

#include "Engine/Deck.h"
#include "Engine/Mixer.h"
#include "Library/Library.h"
#include "UI/DarkLookAndFeel.h"
#include "UI/DeckComponent.h"
#include "UI/LibraryBrowser.h"
#include "UI/MixerComponent.h"

namespace midj
{
// Ventana principal: dos decks con el mezclador en el centro, crossfader, navegador de la
// biblioteca abajo y el motor de audio.
class MainComponent : public juce::AudioAppComponent,
                      public juce::DragAndDropContainer
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
    // Carga usando el analisis guardado en la biblioteca si lo hay (carga mucho mas rapida).
    void loadIntoDeck (Deck& deck, const juce::File& file);
    // Doble clic en el navegador: al primer deck que no este sonando, empezando por A.
    void loadIntoFreeDeck (const juce::File& file);

    DarkLookAndFeel lookAndFeel;
    juce::AudioFormatManager formatManager;
    Deck deckA { formatManager };
    Deck deckB { formatManager };
    Mixer mixer { deckA, deckB };
    Library library { formatManager };

    DeckComponent deckViewA { deckA, deckB, "A" };
    DeckComponent deckViewB { deckB, deckA, "B" };
    MixerComponent mixerView { mixer };
    std::unique_ptr<LibraryBrowser> browser;   // se crea con la biblioteca ya abierta
    juce::TooltipWindow tooltips { this };
    juce::Slider crossfader;
    juce::Label crossfaderLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
}
