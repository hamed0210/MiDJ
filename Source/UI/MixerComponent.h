#pragma once

#include "Engine/Mixer.h"
#include "UI/Icons.h"
#include "UI/MidiLearnOverlay.h"
#include "UI/Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace midj
{
class VideoStage;

// Columna central (diseno "Escenario"). Arriba una tarjeta con el volumen de cada deck a los
// lados (fader vertical y vumetro) y en medio la pagina elegida con las pestanas AUDIO /
// PROGRAMA (que estan en la barra de arriba de la ventana):
//  - AUDIO: para cada deck High, Mid, Low, Gain y Filter con su vumetro; en medio el master
//    (volumen y vumetro de cada lado). Doble clic en un mando: a su posicion neutra.
//  - PROGRAMA: la salida de video (vista previa), lo mas grande que quepa.
// Debajo, una franja con el crossfader (su centro marcado) y en la misma linea la transicion
// entre escenas y su duracion; si no caben, la transicion pasa a una segunda linea.
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
    std::function<void (bool program)> onTabChanged;   // para marcar la pestana de la barra de arriba
    // Ajustes: sin las transiciones, la franja de abajo solo tiene el crossfader.
    void setShowTransitions (bool show) { showTransitions = show; resized(); repaint(); }
    // Con audifonos configurados: boton de pre-escucha bajo cada volumen y, en AUDIO, los mandos
    // de los audifonos (mezcla cue / master y volumen).
    void setHeadphonesAvailable (bool available);
    // Boton LIM bajo el master: poner y quitar el limitador. onLimiterChanged, para guardarlo;
    // limiterSettingChanged(), si se cambio desde Ajustes de audio.
    std::function<void()> onLimiterChanged;
    void limiterSettingChanged() { updateLimiterButton(); }
    // Las transiciones cambiaron (p. ej. se anadio una personalizada): rehacer los botones.
    void refreshTransitions();
    void addCustomTransition();   // boton "+ Personalizada": abre la ventana para crearla

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
        std::unique_ptr<IconButton> pfl;   // pre-escucha en los audifonos
        LevelMeter meter, faderMeter { 12 };
    };

    void setUpChannel (int index);
    void selectTransition (int index);
    void selectCustom (int id);
    juce::String centreText() const;

    Mixer& mixer;
    VideoStage* stage = nullptr;
    bool videoTab = false, showTransitions = true, headphonesAvailable = false;
    LabelledKnob cueMixKnob { "Cue / Mst", 0.0, 1.0, 0.0 }, headphoneKnob { "Auricular", 0.0, 1.0, Mixer::defaultMasterVolume };
    juce::uint32 limiterUntil = 0;   // "LIM" en rojo bajo los volumenes mientras el limitador actua
    bool limiterShown = false;
    void updatePflButtons();
    juce::TextButton limiterButton;   // LIM ON / LIM OFF bajo el master
    void updateLimiterButton();
    std::array<Channel, 2> channels;
    LabelledKnob master { "Master", 0.0, 1.0, Mixer::defaultMasterVolume };
    LevelMeter masterLeft, masterRight;
    juce::Label noVideoLabel;
    CentredSlider crossfader { 0.0, 1.0, 0.02 };
    std::vector<std::unique_ptr<juce::TextButton>> transitionButtons;
    std::vector<int> transitionCustomIds;                       // 0 = una de las de siempre
    std::vector<std::unique_ptr<IconButton>> removeButtons;     // la x de cada propia (nullptr en las de siempre)
    juce::TextButton addTransitionButton { "+" };
    juce::TextButton transitionMs;   // la duracion ("600 ms"); al pulsarlo, la lista de duraciones

    juce::Rectangle<int> pageArea, previewArea, cardArea, centreLabelArea, crossLabelsA, crossLabelsB,
                         masterLabelArea, separatorArea;
    std::array<juce::Rectangle<int>, 2> stripLabelArea, faderTitleArea, faderLabelArea;
    juce::String lastCentreText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerComponent)
};
}
