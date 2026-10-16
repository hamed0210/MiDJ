#include "Engine/BpmDetector.h"

#include <cmath>
#include <vector>

namespace midj::BpmDetector
{
namespace
{
    constexpr int hopSize = 512;             // muestras por punto de envolvente
    constexpr double minBpm = 70.0, maxBpm = 180.0;
    constexpr double foldLow = 85.0, foldHigh = 170.0;

    // Autocorrelacion de los ataques para un retardo entero.
    double autocorrelation (const std::vector<float>& onset, int lag)
    {
        double sum = 0.0;
        for (size_t i = (size_t) lag; i < onset.size(); ++i)
            sum += (double) onset[i] * onset[i - (size_t) lag];
        return sum;
    }
}

double detect (const float* mono, int numSamples, double sampleRate)
{
    if (mono == nullptr || sampleRate <= 0.0 || numSamples < hopSize * 64)
        return 0.0;

    // 1. Envolvente: energia media absoluta por bloque de hopSize muestras.
    const int frames = numSamples / hopSize;
    std::vector<float> envelope ((size_t) frames);
    for (int f = 0; f < frames; ++f)
    {
        float sum = 0.0f;
        for (int i = 0; i < hopSize; ++i)
            sum += std::abs (mono[f * hopSize + i]);
        envelope[(size_t) f] = sum / (float) hopSize;
    }

    // 2. Ataques: solo las subidas de energia.
    std::vector<float> onset ((size_t) frames, 0.0f);
    for (size_t f = 1; f < envelope.size(); ++f)
        onset[f] = std::max (0.0f, envelope[f] - envelope[f - 1]);

    // 3. Retardo con mayor autocorrelacion entre 70 y 180 BPM.
    const double frameRate = sampleRate / hopSize;
    const int lagMin = (int) std::floor (frameRate * 60.0 / maxBpm);
    const int lagMax = (int) std::ceil (frameRate * 60.0 / minBpm);
    if (lagMax + 2 >= frames)
        return 0.0;

    int bestLag = 0;
    double bestScore = 0.0;
    for (int lag = lagMin; lag <= lagMax; ++lag)
    {
        const double score = autocorrelation (onset, lag);
        if (score > bestScore) { bestScore = score; bestLag = lag; }
    }
    if (bestLag == 0 || bestScore <= 0.0)
        return 0.0;

    // 4. Ajuste fino: el pico 4 pulsos mas alla da 4 veces mas precision que el primero.
    //    Se busca alrededor de 4 x bestLag y se afina con una parabola por el maximo y sus vecinos.
    constexpr int beatsAhead = 4;
    double period = bestLag;
    const int farCentre = bestLag * beatsAhead;
    if (farCentre + beatsAhead + 2 < frames)
    {
        int farLag = farCentre;
        double farScore = 0.0;
        for (int lag = farCentre - beatsAhead; lag <= farCentre + beatsAhead; ++lag)
        {
            const double score = autocorrelation (onset, lag);
            if (score > farScore) { farScore = score; farLag = lag; }
        }
        const double a = autocorrelation (onset, farLag - 1);
        const double c = autocorrelation (onset, farLag + 1);
        const double denom = a - 2.0 * farScore + c;
        const double offset = denom != 0.0 ? 0.5 * (a - c) / denom : 0.0;
        period = (farLag + offset) / beatsAhead;
    }
    double bpm = 60.0 * frameRate / period;

    // 5. Plegado de octava al rango habitual de un DJ.
    while (bpm < foldLow)   bpm *= 2.0;
    while (bpm >= foldHigh) bpm *= 0.5;
    return bpm;
}

double detect (juce::AudioFormatReader& reader, double maxSeconds)
{
    const auto total = (int) std::min<juce::int64> (reader.lengthInSamples,
                                                    (juce::int64) (maxSeconds * reader.sampleRate));
    if (total <= 0)
        return 0.0;

    juce::AudioBuffer<float> buffer ((int) reader.numChannels, total);
    reader.read (&buffer, 0, total, 0, true, true);

    // Mezcla a mono.
    std::vector<float> mono ((size_t) total, 0.0f);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        juce::FloatVectorOperations::add (mono.data(), buffer.getReadPointer (ch), total);

    return detect (mono.data(), total, reader.sampleRate);
}
}
