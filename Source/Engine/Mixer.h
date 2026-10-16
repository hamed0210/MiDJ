#pragma once

#include "Engine/ChannelStrip.h"
#include "Engine/Deck.h"

namespace midj
{
// Mezclador de dos decks: cada deck pasa por su canal (ganancia, EQ, filtro, volumen)
// y despues por el crossfader.
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

    static float leftGain (float x);
    static float rightGain (float x) { return leftGain (1.0f - x); }

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;

private:
    Deck& deckA;
    Deck& deckB;
    ChannelStrip stripA, stripB;
    std::atomic<float> crossfader { 0.5f };
    float lastGainA = 1.0f, lastGainB = 1.0f;   // para suavizar el cambio dentro del bloque
    juce::AudioBuffer<float> scratch;
};
}
