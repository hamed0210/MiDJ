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

    // Valor de los ataques en una posicion fraccionaria (interpolacion lineal).
    double onsetAt (const std::vector<float>& onset, double frame)
    {
        const auto i = (size_t) frame;
        if (i + 1 >= onset.size())
            return 0.0;
        const double t = frame - (double) i;
        return onset[i] * (1.0 - t) + onset[i + 1] * t;
    }
}

TempoAnalysis analyse (const float* mono, int numSamples, double sampleRate)
{
    if (mono == nullptr || sampleRate <= 0.0 || numSamples < hopSize * 64)
        return {};

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
        return {};

    int bestLag = 0;
    double bestScore = 0.0;
    for (int lag = lagMin; lag <= lagMax; ++lag)
    {
        const double score = autocorrelation (onset, lag);
        if (score > bestScore) { bestScore = score; bestLag = lag; }
    }
    if (bestLag == 0 || bestScore <= 0.0)
        return {};

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

    // 5. Plegado de octava al rango habitual de un DJ (el periodo cambia igual).
    double bpm = 60.0 * frameRate / period;
    while (bpm < foldLow)   { bpm *= 2.0; period *= 0.5; }
    while (bpm >= foldHigh) { bpm *= 0.5; period *= 2.0; }

    // 6. Fase, con una envolvente 4 veces mas fina (128 muestras, ~3 ms): se prueba cada
    //    desfase dentro de un periodo y se queda el que suma mas ataques en todos los pulsos.
    constexpr int fineHop = 128;
    const int fineFrames = numSamples / fineHop;
    std::vector<float> fineOnset ((size_t) fineFrames, 0.0f);
    float previous = 0.0f;
    for (int f = 0; f < fineFrames; ++f)
    {
        float sum = 0.0f;
        for (int i = 0; i < fineHop; ++i)
            sum += std::abs (mono[f * fineHop + i]);
        fineOnset[(size_t) f] = std::max (0.0f, sum - previous);
        previous = sum;
    }
    const double finePeriod = period * hopSize / fineHop;
    double bestPhase = 0.0, bestPhaseScore = -1.0;
    for (double phase = 0.0; phase < finePeriod; phase += 0.25)
    {
        double score = 0.0;
        for (double f = phase; f < (double) fineFrames - 1.0; f += finePeriod)
            score += onsetAt (fineOnset, f);
        if (score > bestPhaseScore) { bestPhaseScore = score; bestPhase = phase; }
    }
    // 7. Ajuste conjunto de tempo y fase: para cada pulso previsto se busca el ataque real
    //    mas fuerte cerca (+-15 % de periodo) y se traza una recta t(k) = fase + k * periodo
    //    por minimos cuadrados ponderados por la fuerza del ataque. Dos pasadas.
    double phaseFrames = bestPhase, periodFrames = finePeriod;
    for (int pass = 0; pass < 2; ++pass)
    {
        double sw = 0, sk = 0, st = 0, skk = 0, skt = 0;
        const double window = 0.15 * periodFrames;
        for (int k = 0; ; ++k)
        {
            const double expected = phaseFrames + k * periodFrames;
            const int lo = (int) std::ceil (expected - window), hi = (int) std::floor (expected + window);
            if (hi + 1 >= fineFrames)
                break;
            int peak = -1;
            float peakValue = 0.0f;
            for (int f = std::max (1, lo); f <= hi; ++f)
                if (fineOnset[(size_t) f] > peakValue) { peakValue = fineOnset[(size_t) f]; peak = f; }
            if (peak < 0)
                continue;
            // Posicion subfotograma del pico (parabola con sus vecinos).
            const double a = fineOnset[(size_t) peak - 1], b = peakValue, c = fineOnset[(size_t) peak + 1];
            const double denom = a - 2.0 * b + c;
            const double t = peak + (denom != 0.0 ? 0.5 * (a - c) / denom : 0.0);
            const double w = peakValue;
            sw += w; sk += w * k; st += w * t; skk += w * k * k; skt += w * k * t;
        }
        const double det = sw * skk - sk * sk;
        if (sw <= 0.0 || det <= 0.0)
            break;
        periodFrames = (sw * skt - sk * st) / det;
        phaseFrames = (st - periodFrames * sk) / sw;
    }
    if (periodFrames > 0.0 && std::abs (periodFrames / finePeriod - 1.0) < 0.02)   // solo afina, nunca cambia de tempo
        bpm = 60.0 * sampleRate / (periodFrames * fineHop);
    else
        phaseFrames = bestPhase;

    // Un ataque que empieza dentro del fotograma f se ve como subida en f; de media el
    // pulso cae medio fotograma antes.
    const double periodSeconds = 60.0 / bpm;
    double firstBeat = (phaseFrames - 0.5) * fineHop / sampleRate;
    while (firstBeat < 0.0) firstBeat += periodSeconds;
    while (firstBeat >= periodSeconds) firstBeat -= periodSeconds;

    return { bpm, firstBeat };
}

double detect (const float* mono, int numSamples, double sampleRate)
{
    return analyse (mono, numSamples, sampleRate).bpm;
}

TempoAnalysis analyse (const juce::AudioBuffer<float>& audio, double sampleRate, double maxSeconds)
{
    const int total = std::min (audio.getNumSamples(), (int) (maxSeconds * sampleRate));
    if (total <= 0 || audio.getNumChannels() == 0)
        return {};

    std::vector<float> mono ((size_t) total, 0.0f);
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        juce::FloatVectorOperations::add (mono.data(), audio.getReadPointer (ch), total);

    return analyse (mono.data(), total, sampleRate);
}
}
