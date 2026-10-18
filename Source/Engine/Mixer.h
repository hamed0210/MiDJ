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

    // Pre-escucha (audifonos): cada deck con su boton (la senal tras el EQ y el filtro, antes
    // del volumen). Mezcla 0 = solo la pre-escucha, 1 = solo el master; sin ningun deck
    // marcado se oye el master. getCueBuffer(): lo de los audifonos del ultimo bloque (2 canales).
    void setPfl (int deckIndex, bool on) { (deckIndex == 0 ? pflA : pflB) = on; }
    bool getPfl (int deckIndex) const    { return (deckIndex == 0 ? pflA : pflB).load(); }
    void setCueMix (float m)              { cueMix = juce::jlimit (0.0f, 1.0f, m); }
    float getCueMix() const               { return cueMix.load(); }
    void setHeadphoneVolume (float knob)  { headphoneKnob = juce::jlimit (0.0f, 1.0f, knob); }
    float getHeadphoneVolume() const      { return headphoneKnob.load(); }
    const juce::AudioBuffer<float>& getCueBuffer() const { return cue; }

    // Limitador del master: no deja pasar de -0,3 dBFS (ataque inmediato, suelta en 150 ms).
    // takeLimiterGain(): la ganancia mas baja aplicada desde la ultima lectura (1 = no actuo).
    void setLimiterEnabled (bool on) { limiterOn = on; }
    bool isLimiterEnabled() const    { return limiterOn.load(); }
    float takeLimiterGain()          { return limiterMin.exchange (1.0f); }
    static constexpr float limiterCeiling = 0.966f;   // -0,3 dBFS

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
    juce::AudioBuffer<float> scratch, preA, preB, cue;
    std::atomic<bool> pflA { false }, pflB { false }, limiterOn { true };
    std::atomic<float> cueMix { 0.0f }, headphoneKnob { defaultMasterVolume }, limiterMin { 1.0f };
    float lastPflA = 0.0f, lastPflB = 0.0f, lastCueMix = 0.0f, lastHeadphoneGain = 1.0f;
    float limiterGain = 1.0f, limiterRelease = 0.9999f;
};
}
