#pragma once

#include "Engine/Sampler.h"
#include "UI/MidiLearnOverlay.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace midj
{
// Panel del sampler: 8 pads, volumen y STOP.
//  - Clic en un pad con sonido: dispararlo (en modo hold suena mientras se mantiene).
//  - Clic en un pad vacio: elegir un archivo.
//  - Clic derecho: cargar, grabar N pulsos de un deck, modo de disparo, bucle, tempo, quitar.
//  - Arrastrar una cancion del navegador o un archivo del Explorador encima: cargarlo.
class SamplerPanel : public juce::Component,
                     private juce::Timer
{
public:
    SamplerPanel (Sampler& sampler, juce::AudioFormatManager& formats);
    ~SamplerPanel() override;

    std::function<void()> onBankChanged;   // para guardar el banco

    void paint (juce::Graphics& g) override;
    void resized() override;
    void addLearnTargets (std::vector<LearnTarget>& out);

    void loadIntoPad (int pad, const juce::File& file);

private:
    class Pad;
    void timerCallback() override;
    void showMenu (int pad);
    void choosePadFile (int pad);

    Sampler& sampler;
    juce::AudioFormatManager& formats;
    juce::Rectangle<int> titleArea, volumeLabelArea;
    juce::Slider volume;
    juce::TextButton stopButton { "Parar todo" };
    std::array<std::unique_ptr<Pad>, Sampler::numPads> pads;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerPanel)
};
}
