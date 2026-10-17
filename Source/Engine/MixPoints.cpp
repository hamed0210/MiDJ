#include "Engine/MixPoints.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace midj::MixPointAnalyser
{
namespace
{
    // Umbral de "suena" de VirtualDJ: suma de |L|+|R| (16 bits) > 98304 en 256 muestras,
    // es decir una media de 98304 / 32768 / 512 por muestra y canal en escala -1..1.
    constexpr double soundThreshold = 98304.0 / 32768.0 / 512.0;

    void realPoints (const juce::AudioBuffer<float>& audio, double rate, MixPoints& p)
    {
        constexpr int window = 256;
        const int n = audio.getNumSamples();
        const float* l = audio.getReadPointer (0);
        const float* r = audio.getReadPointer (juce::jmin (1, audio.getNumChannels() - 1));
        int first = -1, last = -1;
        for (int w = 0; w + window <= n; w += window)
        {
            double sum = 0.0;
            for (int i = w; i < w + window; ++i)
                sum += std::abs (l[i]) + std::abs (r[i]);
            if (sum / (2.0 * window) > soundThreshold)
            {
                if (first < 0)
                    first = w;
                last = w + window;
            }
        }
        p.realStart = first < 0 ? 0.0 : first / rate;
        p.realEnd = last < 0 ? n / rate : last / rate;
    }

    void fadePoints (const juce::AudioBuffer<float>& audio, double rate, MixPoints& p)
    {
        const int block = (int) std::lround (0.05 * rate);
        const int blocks = audio.getNumSamples() / block;
        if (blocks < 8)
        {
            p.fadeStart = p.realStart;
            p.fadeEnd = p.realEnd;
            return;
        }
        std::vector<double> energy ((size_t) blocks, 0.0);
        for (int b = 0; b < blocks; ++b)
            for (int ch = 0; ch < juce::jmin (2, audio.getNumChannels()); ++ch)
            {
                const float* d = audio.getReadPointer (ch, b * block);
                for (int i = 0; i < block; ++i)
                    energy[(size_t) b] += (double) d[i] * d[i];
            }
        auto sorted = energy;
        std::sort (sorted.begin(), sorted.end());
        const double reference = sorted[(size_t) ((blocks - 1) * 95 / 100)];   // percentil 95
        const double level = reference / 3.0;

        p.fadeStart = p.realStart;
        const int firstQuarter = juce::jmin (blocks / 4, 1024);
        for (int b = 0; b < firstQuarter; ++b)
            if (energy[(size_t) b] >= level) { p.fadeStart = b * (double) block / rate; break; }

        p.fadeEnd = p.realEnd;
        for (int b = blocks - 1; b >= blocks - blocks / 8; --b)
            if (energy[(size_t) b] >= level) { p.fadeEnd = b * (double) block / rate; break; }

        p.fadeStart = juce::jlimit (p.realStart, p.realEnd, p.fadeStart);
        p.fadeEnd = juce::jlimit (p.realStart, p.realEnd, p.fadeEnd);
    }

    void tempoPoints (const juce::AudioBuffer<float>& audio, double rate, const TempoAnalysis& tempo, MixPoints& p)
    {
        if (tempo.bpm <= 0.0)
            return;
        const double period = 60.0 / tempo.bpm;
        const int n = audio.getNumSamples();

        // Graves (paso bajo de 150 Hz): el bombo marca la parte ritmica.
        std::vector<float> low ((size_t) n);
        for (int i = 0; i < n; ++i)
            low[(size_t) i] = 0.5f * (audio.getSample (0, i) + audio.getSample (juce::jmin (1, audio.getNumChannels() - 1), i));
        for (int stage = 0; stage < 2; ++stage)
        {
            juce::IIRFilter f;
            f.setCoefficients (juce::IIRCoefficients::makeLowPass (rate, 150.0));
            f.processSamples (low.data(), n);
        }

        // Fuerza de cada pulso: cuanto SUBE la energia de los graves en el pulso (80 ms despues
        // menos 80 ms antes). Un sonido sostenido no sube: no cuenta como golpe.
        const int beats = (int) ((n / rate - tempo.firstBeat) / period);
        if (beats < 40)
            return;
        const int span = (int) (0.08 * rate);
        const auto energyIn = [&] (int from, int to)
        {
            double e = 0.0;
            for (int i = juce::jmax (0, from); i < juce::jmin (n, to); ++i)
                e += (double) low[(size_t) i] * low[(size_t) i];
            return e;
        };
        std::vector<double> strength ((size_t) beats, 0.0);
        double level = 0.0;
        for (int k = 0; k < beats; ++k)
        {
            const int s = (int) ((tempo.firstBeat + k * period) * rate);
            const double after = energyIn (s, s + span);
            strength[(size_t) k] = juce::jmax (0.0, after - energyIn (s - span, s));
            level += after / beats;
        }
        auto sorted = strength;
        std::sort (sorted.begin(), sorted.end());
        const double reference = sorted[(size_t) ((beats - 1) * 90 / 100)];
        if (reference < 0.1 * level || reference <= 0.0)
            return;   // ningun golpe destaca sobre el sonido de fondo: no hay parte ritmica
        // En la musica real los pulsos alternos suelen ser mas suaves (el bombo del dembow, por
        // ejemplo): un pulso cuenta como golpe si llega al 30 % de la referencia.
        const auto strong = [&] (int k) { return strength[(size_t) k] >= 0.3 * reference; };

        // Regla de VirtualDJ: 3 pulsos fuertes seguidos y menos de 5 flojos en los 32 siguientes.
        const auto startsRhythm = [&] (int k, int dir)
        {
            for (int j = 0; j < 3; ++j)
                if (! strong (k + dir * j))
                    return false;
            int weak = 0;
            for (int j = 3; j < 35; ++j)
                if (! strong (k + dir * j))
                    ++weak;
            return weak < 5;
        };

        int startBeat = -1, endBeat = -1;
        for (int k = 0; k < beats / 3 && k + 35 < beats; ++k)
            if (startsRhythm (k, 1)) { startBeat = k; break; }
        for (int k = beats - 1; k > beats - beats / 4 && k - 35 >= 0; --k)
            if (startsRhythm (k, -1)) { endBeat = k; break; }
        // Cada punto por separado, como en VirtualDJ (una cancion puede empezar con ritmo y
        // acabar sin el). Al compas: el inicio se adelanta al compas que lo contiene; el final
        // se lleva al final de su compas.
        if (startBeat >= 0)
            p.tempoStart = tempo.firstBeat + (startBeat / 4 * 4) * period;
        if (endBeat >= 0 && endBeat > startBeat)
            p.tempoEnd = tempo.firstBeat + juce::jmin (beats, (endBeat / 4 + 1) * 4) * period;
    }
}

MixPoints analyse (const juce::AudioBuffer<float>& audio, double sampleRate, const TempoAnalysis& tempo)
{
    MixPoints p;
    if (audio.getNumSamples() == 0 || audio.getNumChannels() == 0 || sampleRate <= 0.0)
        return p;
    realPoints (audio, sampleRate, p);
    fadePoints (audio, sampleRate, p);
    tempoPoints (audio, sampleRate, tempo, p);
    return p;
}
}
