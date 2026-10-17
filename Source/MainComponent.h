#pragma once

#include "Control/ControlSurface.h"
#include "Control/MidiController.h"
#include "Engine/Deck.h"
#include "Engine/Mixer.h"
#include "Library/Library.h"
#include "UI/DarkLookAndFeel.h"
#include "UI/DeckComponent.h"
#include "UI/LibraryBrowser.h"
#include "UI/MidiLearnOverlay.h"
#include "UI/MixerComponent.h"
#include "UI/SamplerPanel.h"

namespace midj
{
// Ventana principal: dos decks con el mezclador en el centro, crossfader, navegador de la
// biblioteca abajo y el motor de audio.
class MainComponent : public juce::AudioAppComponent,
                      public juce::DragAndDropContainer,
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

private:
    // Carga usando el analisis guardado en la biblioteca si lo hay (carga mucho mas rapida).
    bool loadIntoDeck (Deck& deck, const juce::File& file);
    // Doble clic en el navegador: al primer deck que no este sonando, empezando por A.
    void loadIntoFreeDeck (const juce::File& file);
    void toggleMidiLearn();
    void saveSamplerBank();
    static juce::File getDataFolder() { return Library::getDefaultFile().getParentDirectory(); }
    void timerCallback() override;   // el crossfader sigue a la controladora
    // Modo de prueba sin controladora: --simular-midi archivo (ver README).
    void startMidiSimulation (const juce::File& script);

    DarkLookAndFeel lookAndFeel;
    juce::AudioFormatManager formatManager;
    Deck deckA { formatManager };
    Deck deckB { formatManager };
    Mixer mixer { deckA, deckB };
    Library library { formatManager };
    Automix automix { deckA, deckB, mixer };
    ControlSurface controlSurface { deckA, deckB, mixer };
    MidiController midi { controlSurface };

    DeckComponent deckViewA { deckA, deckB, mixer.getEffects (0), "A" };
    DeckComponent deckViewB { deckB, deckA, mixer.getEffects (1), "B" };
    MixerComponent mixerView { mixer };
    std::unique_ptr<LibraryBrowser> browser;   // se crea con la biblioteca ya abierta
    SamplerPanel samplerPanel { mixer.getSampler(), formatManager };
    juce::TooltipWindow tooltips { this };
    juce::Slider crossfader;
    juce::Label crossfaderLabel;
    juce::TextButton midiButton { "MIDI" };
    MidiLearnOverlay midiOverlay { midi };
    int timerTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
}
