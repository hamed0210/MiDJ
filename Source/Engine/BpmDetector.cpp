#include "Engine/BpmDetector.h"

#include <algorithm>
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

    // Paso bajo de 150 Hz (dos biquads, 24 dB/octava) que deja el bombo y el bajo.
    // Es causal (nada antes del golpe: un filtro de fase cero "suena" un poco antes y en escala
    // logaritmica eso ya parece un ataque) y se adelanta su retardo en graves, 2 x sqrt(2) / (2 pi fc).
    std::vector<float> lowPass (const float* mono, int numSamples, double sampleRate)
    {
        constexpr double cutoff = 150.0;
        std::vector<float> out (mono, mono + numSamples);   // se filtra y se adelanta en el mismo vector
        for (int stage = 0; stage < 2; ++stage)
        {
            juce::IIRFilter filter;
            filter.setCoefficients (juce::IIRCoefficients::makeLowPass (sampleRate, cutoff));
            filter.processSamples (out.data(), numSamples);
        }
        const auto delay = std::min ((size_t) numSamples,
                                     (size_t) std::lround (2.0 * std::sqrt (2.0) / (2.0 * juce::MathConstants<double>::pi * cutoff) * sampleRate));
        std::move (out.begin() + (std::ptrdiff_t) delay, out.end(), out.begin());
        std::fill (out.end() - (std::ptrdiff_t) delay, out.end(), 0.0f);
        return out;
    }
    // Ataques: subidas del logaritmo de la energia medida en una ventana de 23 ms que termina
    // al final de cada bloque de 'hop' muestras; normalizados a media 1.
    //  - La ventana larga ignora la oscilacion de la propia onda (un bajo de 50 Hz tiene
    //    ciclos de 20 ms) y, al terminar en el bloque, una subida en f empieza dentro de f.
    //  - En escala logaritmica la subida mayor es la del primer instante (de casi nada a algo),
    //    asi un sonido sostenido marca su comienzo y no el final de la ventana.
    std::vector<float> rises (const float* signal, int numSamples, int hop)
    {
        constexpr int window = 1024;
        jassert (window % hop == 0);
        const int blocksPerWindow = window / hop;
        const int frames = numSamples / hop;
        // Energia (x^2) de cada bloque de 'hop' muestras: la ventana de un fotograma es la suma de
        // sus ultimos bloques (asi no hace falta una suma acumulada por muestra, 40 MB en 2 minutos).
        std::vector<double> blockEnergy ((size_t) frames, 0.0);
        double allEnergy = 0.0;
        for (int b = 0; b < frames; ++b)
        {
            double e = 0.0;
            for (const float* x = signal + b * hop, * end = x + hop; x < end; ++x)
                e += (double) *x * *x;
            blockEnergy[(size_t) b] = e;
            allEnergy += e;
        }
        for (int i = frames * hop; i < numSamples; ++i)   // la cola que no llena un bloque cuenta para el suelo
            allEnergy += (double) signal[i] * signal[i];

        // Suelo de ruido: una diezmilesima de la energia media (-40 dB), para que el silencio
        // no genere subidas enormes en el logaritmo.
        const double floor = 1e-4 * allEnergy / std::max (1, numSamples) + 1e-12;
        std::vector<float> out ((size_t) frames, 0.0f);
        double previous = 0.0, total = 0.0;
        for (int f = 0; f < frames; ++f)
        {
            double windowEnergy = 0.0;
            for (int k = std::max (0, f - blocksPerWindow + 1); k <= f; ++k)
                windowEnergy += blockEnergy[(size_t) k];
            const double level = std::log (windowEnergy / window + floor);
            out[(size_t) f] = f > 0 ? (float) std::max (0.0, level - previous) : 0.0f;
            previous = level;
            total += out[(size_t) f];
        }
        if (total > 0.0)
            for (auto& v : out)
                v = (float) (v * frames / total);
        return out;
    }
    // Ataques de la mezcla + ataques de los graves con el doble de peso.
    std::vector<float> combinedOnset (const float* mono, const float* low, int numSamples, int hop)
    {
        auto onset = rises (mono, numSamples, hop);
        const auto lowOnset = rises (low, numSamples, hop);
        for (size_t i = 0; i < onset.size(); ++i)
            onset[i] += 2.0f * lowOnset[i];
        return onset;
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

    // 1. Ataques combinando dos bandas: toda la mezcla y los graves (< 150 Hz, bombo y bajo).
    //    En estilos como el reggaeton las cajas fuera de pulso suenan mas que el bombo en la
    //    mezcla completa; la banda grave asegura que el pulso real pese lo suficiente.
    const std::vector<float> lowBand = lowPass (mono, numSamples, sampleRate);
    const std::vector<float> onset = combinedOnset (mono, lowBand.data(), numSamples, hopSize);
    const int frames = (int) onset.size();

    // 2. Autocorrelacion de los ataques, normalizada por la cantidad de datos de cada retardo.
    const double frameRate = sampleRate / hopSize;
    const int lagMin = (int) std::floor (frameRate * 60.0 / maxBpm);
    const int lagMax = (int) std::ceil (frameRate * 60.0 / minBpm);
    constexpr int combTeeth = 4;
    if (lagMax * combTeeth + 8 >= frames)
        return {};
    std::vector<double> ac ((size_t) (lagMax * combTeeth + 8), 0.0);
    for (size_t lag = 1; lag < ac.size(); ++lag)
        ac[lag] = autocorrelation (onset, (int) lag) / (double) (frames - (int) lag);

    // 3. Tempo con un "peine": el pulso de verdad se repite a 1, 2, 3 y 4 pulsos. Un falso
    //    pulso (por ejemplo 3/4 del real, tipico del dembow) falla en alguno de esos dientes.
    //    Se prueban retardos fraccionarios: con uno entero, 4 x 54 no da en el pico de 4 x 54,4.
    auto acAt = [&ac] (double lag)
    {
        const auto i = (size_t) lag;
        const double t = lag - (double) i;
        return ac[i] * (1.0 - t) + ac[i + 1] * t;
    };
    double bestLag = 0.0, bestScore = 0.0;
    for (double lag = lagMin; lag <= lagMax; lag += 0.1)
    {
        double score = 0.0;
        for (int k = 1; k <= combTeeth; ++k)
            score += acAt (k * lag);
        if (score > bestScore) { bestScore = score; bestLag = lag; }
    }
    if (bestLag <= 0.0 || bestScore <= 0.0)
        return {};
    // 4. Ajuste fino: el pico 4 pulsos mas alla da 4 veces mas precision que el primero.
    //    Se busca alrededor de 4 x bestLag y se afina con una parabola por el maximo y sus vecinos.
    constexpr int beatsAhead = 4;
    double period = bestLag;
    const int farCentre = (int) std::lround (bestLag * beatsAhead);
    if (farCentre + beatsAhead + 2 < frames)
    {
        int farLag = farCentre;
        double farScore = 0.0;
        for (int lag = farCentre - beatsAhead; lag <= farCentre + beatsAhead; ++lag)
        {
            const double score = ac[(size_t) lag];
            if (score > farScore) { farScore = score; farLag = lag; }
        }
        const double a = ac[(size_t) farLag - 1];
        const double c = ac[(size_t) farLag + 1];
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
    const std::vector<float> fineOnset = combinedOnset (mono, lowBand.data(), numSamples, fineHop);
    const int fineFrames = (int) fineOnset.size();
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

    // Paso de fotograma a segundos. Con la subida logaritmica el pico (interpolado) cae un
    // tercio de fotograma antes del golpe; medido con golpes sinteticos en tres posiciones
    // distintas dentro del fotograma (0,13 / 0,45 / 0,77), el desfase es el mismo en todas.
    constexpr double peakToOnsetFrames = 1.0 / 3.0;
    const double periodSeconds = 60.0 / bpm;
    double firstBeat = (phaseFrames + peakToOnsetFrames) * fineHop / sampleRate;
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
