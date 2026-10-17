#pragma once

#include "Engine/Mixer.h"
#include "UI/MidiLearnOverlay.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace midj
{
class VideoStage;

// Crossfader como el de VirtualDJ: recuadro con A y B a los lados, la linea de cada lado en el
// color del deck que se va apagando al llevarlo hacia el otro, marca del centro arriba.
class CrossfaderSlider : public juce::Slider
{
public:
    CrossfaderSlider();
    void paint (juce::Graphics& g) override;
};

// Columna central del mezclador, como la de VirtualDJ, con dos pestanas:
//  - AUDIO: para cada deck, mandos de GAIN, HIGH, MID, LOW y FILTER y un fader de volumen.
//  - VIDEO: la vista previa del programa, el nivel de video de cada deck y la transicion.
// Abajo, en las dos, el crossfader. Doble clic en un mando lo devuelve a su posicion neutra.
class MixerComponent : public juce::Component,
                       private juce::Timer
{
public:
    explicit MixerComponent (Mixer& mixer);
    ~MixerComponent() override;

    // El video (puede no haber si Direct3D no arranca). Antes de mostrar la pestana VIDEO.
    void setVideoStage (VideoStage* stage);
    void showVideoTab (bool video);
    bool isVideoTabShowing() const { return videoTab; }

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Controles asignables por MIDI (gain, agudos, medios, graves, filtro y volumen de cada deck,
    // y el crossfader).
    void addLearnTargets (std::vector<LearnTarget>& out);
    juce::Slider& getCrossfader() { return crossfader; }

private:
    // Si una controladora mueve el motor, los mandos de la pantalla lo siguen.
    void timerCallback() override;

    struct Channel
    {
        std::array<juce::Slider, 5> knobs;   // GAIN, HIGH, MID, LOW, FILTER
        juce::Slider volume;
        juce::Slider videoLevel;             // pestana VIDEO
    };
    static constexpr std::array<const char*, 5> knobNames { "GAIN", "HIGH", "MID", "LOW", "FILTER" };

    void setUpChannel (Channel& channel, ChannelStrip& strip, juce::Colour accent);
    void refreshTransition();

    Mixer& mixer;
    VideoStage* stage = nullptr;
    bool videoTab = false;
    std::array<Channel, 2> channels;
    std::array<juce::Label, 5> rowLabels;
    juce::Label volumeLabel;
    juce::TextButton audioTabButton { "AUDIO" }, videoTabButton { "VIDEO" };
    juce::Label transitionLabel { {}, "TRANSICION" }, noVideoLabel;
    juce::ComboBox transitionType;
    juce::Slider transitionMs;
    CrossfaderSlider crossfader;
    juce::Rectangle<int> previewArea, faderArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerComponent)
};
}
