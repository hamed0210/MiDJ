#pragma once

#include "Engine/AuxAudio.h"
#include "Engine/ChannelStrip.h"
#include "Engine/Deck.h"
#include "Engine/Effects.h"
#include "Engine/MasterTap.h"
#include "Engine/Sampler.h"

namespace midj
{
// Mezclador de dos decks: cada deck pasa por sus efectos, su canal (ganancia, EQ, filtro,
// volumen) y despues por el crossfader.
// Curva "smooth" como la de VirtualDJ: cada lado va a volumen completo hasta
// que el crossfader pasa de un tercio hacia el otro lado, y despues baja en linea recta.
class Mixer : public juce::AudioSource
{
public:
    Mixer (Deck& left, Deck& right);

    // 0 = solo deck izquierdo, 1 = solo deck derecho.
    void setCrossfader (float position) { crossfader = juce::jlimit (0.0f, 1.0f, position); }
    float getCrossfader() const         { return crossfader.load(); }

    ChannelStrip& getStrip (int deckIndex) { return deckIndex == 0 ? stripA : stripB; }
    EffectRack& getEffects (int deckIndex) { return deckIndex == 0 ? effectsA : effectsB; }
    Sampler& getSampler() { return sampler; }
    // Lo que sale por el master (para el visualizador del video).
    MasterTap& getTap() { return tap; }
    const MasterTap& getTap() const { return tap; }
    // Sonidos que no son de los decks (fuentes de video con audio, capturadoras): al master.
    AuxBus& getAuxBus() { return aux; }
    // Volumen del master: mando de 0 a 1; 0,8 deja el sonido como esta (por encima sube hasta +3,9 dB).
    void setMasterVolume (float knob) { masterKnob = juce::jlimit (0.0f, 1.0f, knob); }
    float getMasterVolume() const     { return masterKnob.load(); }
    static constexpr float defaultMasterVolume = 0.8f;
    static float masterGainFromKnob (float k) { k = juce::jlimit (0.0f, 1.0f, k) / defaultMasterVolume; return k * k; }
    // Vumetros: el pico desde la ultima lectura (y lo pone a cero). Deck: tras su canal (EQ, filtro
    // y volumen), antes del crossfader. Master: cada lado de la salida.
    float takeDeckPeak (int deckIndex) { return (deckIndex == 0 ? peakA : peakB).exchange (0.0f); }
    float takeMasterPeak (int channel) { return (channel == 0 ? peakL : peakR).exchange (0.0f); }
    // Tempo que reciben los efectos de un deck: su BPM y su posicion en pulsos si esta sonando;
    // si no, un pulso libre de 0,5 s.
    static TempoInfo tempoFor (const Deck& deck, double sampleRate);

    static float leftGain (float x);
    static float rightGain (float x) { return leftGain (1.0f - x); }

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;

private:
    Deck& deckA;
    Deck& deckB;
    ChannelStrip stripA, stripB;
    EffectRack effectsA, effectsB;
    Sampler sampler;   // va a la salida general, despues del crossfader
    MasterTap tap;
    AuxBus aux;
    double outputRate = 44100.0;
    std::atomic<float> crossfader { 0.5f };
    std::atomic<float> masterKnob { defaultMasterVolume };
    float lastMasterGain = 1.0f;
    std::atomic<float> peakA { 0.0f }, peakB { 0.0f }, peakL { 0.0f }, peakR { 0.0f };
    static void raisePeak (std::atomic<float>& peak, float value);
    float lastGainA = 1.0f, lastGainB = 1.0f;   // para suavizar el cambio dentro del bloque
    juce::AudioBuffer<float> scratch;
};
}
