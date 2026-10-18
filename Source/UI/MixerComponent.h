#pragma once

#include "Engine/Mixer.h"
#include "UI/MidiLearnOverlay.h"
#include "UI/Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace midj
{
class VideoStage;

// Columna central (diseno "Escenario"), con dos pestanas arriba:
//  - AUDIO: para cada deck High, Mid, Low, Gain y Filter con su vumetro; en medio el master
//    (volumen y vumetro de cada lado). Doble clic en un mando: a su posicion neutra.
//  - PROGRAMA: la salida de video (vista previa) y el nivel de video de cada deck.
// Debajo, siempre, la tarjeta del crossfader: el volumen de cada deck a los lados (fader
// vertical y vumetro), el crossfader con su centro marcado y la transicion entre escenas.
class MixerComponent : public juce::Component,
                       private juce::Timer
{
public:
    explicit MixerComponent (Mixer& mixer);
    ~MixerComponent() override;

    // El video (puede no haber si Direct3D no arranca). Antes de mostrar la pestana PROGRAMA.
    void setVideoStage (VideoStage* stage);
    void showVideoTab (bool program);
    bool isVideoTabShowing() const { return videoTab; }
    // Las transiciones cambiaron (p. ej. se anadio una personalizada): rehacer los botones.
    void refreshTransitions();
    std::function<void()> onAddCustomTransition;   // boton "+ Personalizada"

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Controles asignables por MIDI (gain, agudos, medios, graves, filtro y volumen de cada deck,
    // master y crossfader).
    void addLearnTargets (std::vector<LearnTarget>& out);
    juce::Slider& getCrossfader() { return crossfader; }

private:
    // Sigue al motor (controladora, automix) y mueve los vumetros.
    void timerCallback() override;

    struct Channel
    {
        // Orden: HIGH, MID, LOW, GAIN, FILTER (como se ven, en dos filas).
        std::array<std::unique_ptr<LabelledKnob>, 5> knobs;
        juce::Slider volume;
        juce::Slider videoLevel;   // pestana PROGRAMA
        LevelMeter meter, faderMeter { 12 };
    };

    void setUpChannel (int index);
    void selectTransition (int index);
    juce::String centreText() const;

    Mixer& mixer;
    VideoStage* stage = nullptr;
    bool videoTab = false;
    std::array<Channel, 2> channels;
    LabelledKnob master { "Master", 0.0, 1.0, Mixer::defaultMasterVolume };
    LevelMeter masterLeft, masterRight;
    juce::TextButton audioTabButton { "AUDIO" }, videoTabButton { "PROGRAMA" };
    juce::Label noVideoLabel;
    CentredSlider crossfader { 0.0, 1.0, 0.02 };
    std::vector<std::unique_ptr<juce::TextButton>> transitionButtons;
    juce::TextButton addTransitionButton { "+ Personalizada" };
    juce::Slider transitionMs;

    juce::Rectangle<int> tabsArea, pageArea, previewArea, cardArea, centreLabelArea, crossLabelsA, crossLabelsB,
                         transitionLabelArea, msLabelArea, masterLabelArea, videoLabelArea;
    std::array<juce::Rectangle<int>, 2> stripLabelArea, faderLabelArea, videoLevelLabel;
    juce::String lastCentreText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerComponent)
};
}
