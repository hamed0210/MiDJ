#pragma once

#include "Engine/Mixer.h"
#include "UI/MidiLearnOverlay.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace midj
{
// Columna central del mezclador: para cada deck, mandos de GAIN, HIGH, MID, LOW y FILTER
// y un fader de volumen. Doble clic en un mando lo devuelve a su posicion neutra.
class MixerComponent : public juce::Component,
                       private juce::Timer
{
public:
    explicit MixerComponent (Mixer& mixer);

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Controles asignables por MIDI (gain, agudos, medios, graves, filtro y volumen de cada deck).
    void addLearnTargets (std::vector<LearnTarget>& out);

private:
    // Si una controladora mueve el motor, los mandos de la pantalla lo siguen.
    void timerCallback() override;

    struct Channel
    {
        std::array<juce::Slider, 5> knobs;   // GAIN, HIGH, MID, LOW, FILTER
        juce::Slider volume;
    };
    static constexpr std::array<const char*, 5> knobNames { "GAIN", "HIGH", "MID", "LOW", "FILTER" };

    void setUpChannel (Channel& channel, ChannelStrip& strip, juce::Colour accent);

    Mixer& mixer;
    std::array<Channel, 2> channels;
    std::array<juce::Label, 5> rowLabels;
    juce::Label volumeLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerComponent)
};
}
