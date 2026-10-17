#pragma once

#include "Engine/BpmDetector.h"

namespace midj
{
// Puntos de automix de una cancion, como los POI "automix" de VirtualDJ (segundos; -1 = no hay).
struct MixPoints
{
    double realStart = 0.0, realEnd = 0.0;    // donde empieza y acaba el sonido (sin silencios)
    double fadeStart = 0.0, fadeEnd = 0.0;    // donde entrar y salir con un fundido
    double tempoStart = -1.0, tempoEnd = -1.0; // inicio y fin de la parte ritmica (para mezclar a tempo)

    bool hasTempoSection() const { return tempoStart >= 0.0 && tempoEnd > tempoStart; }
    bool hasTempoStart() const   { return tempoStart >= 0.0; }
    bool hasTempoEnd() const     { return tempoEnd > 0.0; }
};

namespace MixPointAnalyser
{
    // Calcula los puntos con las reglas medidas en VirtualDJ:
    //  - real: ventanas de 256 muestras; "suena" si la media de |L|+|R| pasa de unos -44 dBFS.
    //  - fade: energia en bloques de 50 ms; referencia = percentil 95; fadeStart es el primer bloque
    //    del primer cuarto que llega a 1/3 de la referencia, fadeEnd el ultimo del octavo final.
    //  - tempo: fuerza de los graves en cada pulso de la rejilla; la parte ritmica empieza en el
    //    primer pulso (del primer tercio) en que los pulsos 1-3 siguientes son fuertes y menos de 5
    //    de los 32 siguientes son flojos; el final se busca igual desde atras en el ultimo cuarto.
    //    Se redondean al compas (4 pulsos) contado desde el primer pulso de la rejilla.
    MixPoints analyse (const juce::AudioBuffer<float>& audio, double sampleRate, const TempoAnalysis& tempo);
}
}
