#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

// Detecta el tempo (BPM) y la posicion del primer pulso de una cancion.
// Metodo: ataques (subidas de energia) de la mezcla y de los graves -> autocorrelacion
// -> tempo con un "peine" de 1-4 pulsos en los retardos de 70-180 BPM, ajuste fino con el
// pico 4 pulsos mas alla y plegado de octava al rango 85-170 BPM.
// La fase sale de plegar los ataques sobre el periodo encontrado.
namespace midj
{
struct TempoAnalysis
{
    double bpm = 0.0;          // 0 si no hay un tempo claro
    double firstBeat = 0.0;    // segundos hasta el primer pulso, en [0, 60/bpm)

    double beatPeriod() const { return bpm > 0.0 ? 60.0 / bpm : 0.0; }
};

namespace BpmDetector
{
    // Analiza muestras mono.
    TempoAnalysis analyse (const float* mono, int numSamples, double sampleRate);

    // Atajo: solo el BPM.
    double detect (const float* mono, int numSamples, double sampleRate);

    // Mezcla a mono los canales de un bufer y analiza como mucho maxSeconds.
    TempoAnalysis analyse (const juce::AudioBuffer<float>& audio, double sampleRate, double maxSeconds = 120.0);
}
}
