#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>

namespace midj
{
// Lo que sale por el master, para el visualizador: las ultimas muestras (mono) y el pulso del
// deck que manda. El hilo de audio escribe sin bloquearse; el del video lee lo que se esta
// oyendo ahora (lo escrito hace una latencia de salida), no lo ultimo que se calculo.
class MasterTap
{
public:
    static constexpr int capacity = 1 << 14;   // ~0,37 s a 44,1 kHz

    struct Beat
    {
        bool valid = false;    // hay un deck sonando con BPM
        double beat = 0.0;     // pulso que se oye ahora (la parte entera cuenta pulsos desde el primero)
        double bpm = 0.0;
    };

    void prepare (double sampleRate);
    // Muestras de retraso entre escribir y oirse (latencia de la tarjeta + un bloque).
    void setOutputLatency (int samples) { latency = juce::jmax (0, samples); }
    double getSampleRate() const { return rate.load(); }

    // Hilo de audio: el bloque del master (r puede ser nullptr si es mono) y el pulso del deck
    // que manda al terminar el bloque.
    void push (const float* left, const float* right, int numSamples);
    void setBeat (bool valid, double beatAtEnd, double bpm);

    // Cualquier hilo: las n muestras que acaban en lo que se oye en 'nowMs'
    // (juce::Time::getMillisecondCounterHiRes). Con silencio si aun no hay tantas.
    void read (float* dest, int n, double nowMs) const;
    Beat beatAt (double nowMs) const;

private:
    // Posicion (muestras escritas en total) que se oye en nowMs.
    juce::int64 heardPosition (double nowMs) const;

    std::array<float, capacity> ring {};
    std::atomic<juce::int64> written { 0 };
    std::atomic<double> rate { 44100.0 };
    std::atomic<int> latency { 0 };

    // Momento del ultimo bloque y el pulso: un "seqlock" (el escritor nunca espera; el lector
    // repite si leyo a medias).
    std::atomic<juce::uint32> seq { 0 };
    std::atomic<double> blockMs { 0.0 }, beatAtEnd { 0.0 }, beatBpm { 0.0 };
    std::atomic<juce::int64> blockEnd { 0 };
    std::atomic<bool> beatValid { false };
};
}
