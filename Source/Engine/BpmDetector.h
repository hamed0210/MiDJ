#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

// Detecta el tempo (BPM) de una cancion.
// Metodo: envolvente de energia -> ataques (subidas de energia) -> autocorrelacion
// de los ataques en los retardos que corresponden a 70-180 BPM, con ajuste fino
// parabolico y plegado de octava al rango 85-170 BPM.
namespace midj::BpmDetector
{
    // Analiza muestras mono. Devuelve 0 si no encuentra un tempo claro.
    double detect (const float* mono, int numSamples, double sampleRate);

    // Lee como mucho maxSeconds de un archivo y analiza esa parte.
    double detect (juce::AudioFormatReader& reader, double maxSeconds = 120.0);
}
