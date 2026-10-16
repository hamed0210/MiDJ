// Pruebas automaticas del motor. Se ejecutan con: ctest --preset debug
// Devuelven codigo 0 si todo pasa y 1 si algo falla. No necesitan tarjeta de sonido:
// el audio se "reproduce" pidiendo bloques al deck directamente.
#include "Engine/BpmDetector.h"
#include "Engine/ChannelStrip.h"
#include "Engine/Deck.h"
#include "Engine/Mixer.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
int failures = 0;
constexpr double sr = 44100.0;
constexpr int blockSize = 512;

void check (bool ok, const char* what, double got, double expected)
{
    std::printf ("%s  %-52s obtenido %.4f, esperado %.4f\n", ok ? "[OK]  " : "[FALLO]", what, got, expected);
    if (! ok)
        ++failures;
}

// Pista de bombos sinteticos: un golpe grave con caida rapida en cada pulso.
std::vector<float> clickTrack (double bpm, double seconds, double sampleRate, double firstBeat = 0.25)
{
    std::vector<float> out ((size_t) (seconds * sampleRate), 0.0f);
    const double period = 60.0 / bpm;
    for (double t = firstBeat; t < seconds; t += period)
    {
        const auto start = (size_t) (t * sampleRate);
        for (size_t i = 0; i < (size_t) (0.15 * sampleRate) && start + i < out.size(); ++i)
        {
            const double ph = (double) i / sampleRate;
            out[start + i] += (float) (0.8 * std::exp (-ph * 30.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * ph));
        }
    }
    return out;
}

juce::AudioBuffer<float> toBuffer (const std::vector<float>& mono)
{
    juce::AudioBuffer<float> b (1, (int) mono.size());
    b.copyFrom (0, 0, mono.data(), (int) mono.size());
    return b;
}

// Pide 'seconds' de audio al deck en bloques, como haria la tarjeta de sonido.
void render (midj::Deck& deck, double seconds)
{
    juce::AudioBuffer<float> out (2, blockSize);
    for (int done = 0; done < (int) (seconds * sr); done += blockSize)
    {
        juce::AudioSourceChannelInfo info (&out, 0, blockSize);
        deck.getNextAudioBlock (info);
    }
}

double circularDistance (double a, double b)
{
    const double d = std::abs (a - b);
    return std::min (d, 1.0 - d);
}

void testBpm (double bpm, double sampleRate, double expected)
{
    const auto audio = clickTrack (bpm, 30.0, sampleRate);
    const double got = midj::BpmDetector::detect (audio.data(), (int) audio.size(), sampleRate);
    char name[96];
    std::snprintf (name, sizeof (name), "BPM de una pista a %.1f BPM, %.0f Hz", bpm, sampleRate);
    check (std::abs (got - expected) < 0.02, name, got, expected);
}

// Ritmo de dembow (reggaeton): bombo en cada pulso y caja fuerte en el 3/4 y el 6/4 de cada
// dos pulsos (3+3+2 semicorcheas). Las cajas suenan mas que el bombo en toda la banda, y su
// separacion (3/4 de pulso) engaña a un detector que solo mira la energia total: da 4/3 del tempo.
std::vector<float> dembowTrack (double bpm, double seconds, double firstBeat)
{
    std::vector<float> out ((size_t) (seconds * sr), 0.0f);
    const double period = 60.0 / bpm;
    juce::Random rng (7);
    auto hit = [&] (double t, bool snare)
    {
        const auto start = (size_t) (t * sr);
        for (size_t i = 0; i < (size_t) (0.12 * sr) && start + i < out.size(); ++i)
        {
            const double ph = (double) i / sr;
            out[start + i] += snare ? (float) (0.9 * std::exp (-ph * 40.0) * (rng.nextFloat() * 2.0f - 1.0f))
                                    : (float) (0.6 * std::exp (-ph * 25.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * ph));
        }
    };
    for (int beat = 0; firstBeat + beat * period < seconds; ++beat)
    {
        const double t = firstBeat + beat * period;
        hit (t, false);
        if (beat % 2 == 0)
        {
            hit (t + 0.75 * period, true);
            hit (t + 1.50 * period, true);
        }
    }
    return out;
}

void testDembow (double bpm, double firstBeat)
{
    const auto audio = dembowTrack (bpm, 40.0, firstBeat);
    const auto a = midj::BpmDetector::analyse (audio.data(), (int) audio.size(), sr);
    char name[96];
    std::snprintf (name, sizeof (name), "dembow a %.0f BPM: tempo (no 4/3)", bpm);
    check (std::abs (a.bpm - bpm) < 0.05, name, a.bpm, bpm);
    std::snprintf (name, sizeof (name), "dembow a %.0f BPM: pulso en el bombo (ms)", bpm);
    const double period = 60.0 / bpm;
    double err = std::fmod (a.firstBeat - firstBeat + 10.0 * period, period);
    err = std::min (err, period - err);
    check (err < 0.004, name, err * 1000.0, 0.0);
}

void testPhase (double bpm, double firstBeat)
{
    const auto audio = clickTrack (bpm, 30.0, sr, firstBeat);
    const auto a = midj::BpmDetector::analyse (audio.data(), (int) audio.size(), sr);
    // La rejilla se da dentro del primer periodo: 0,5 s a 128 BPM equivale a 0,03125 s.
    const double period = 60.0 / bpm;
    const double expected = std::fmod (firstBeat, period);
    const double error = circularDistance (a.firstBeat / period, expected / period) * period;
    char name[96];
    std::snprintf (name, sizeof (name), "primer pulso a %.1f BPM (margen 2 ms)", bpm);
    check (error < 0.002, name, a.firstBeat, expected);
}

void testDeck()
{
    juce::AudioFormatManager formats;
    midj::Deck deck (formats);
    deck.prepareToPlay (blockSize, sr);
    deck.loadBuffer (toBuffer (clickTrack (120.0, 30.0, sr)), sr, "clicks120", { 120.0, 0.25 });

    // Reproduccion normal: el tiempo avanza a la velocidad real.
    deck.play();
    render (deck, 2.0);
    check (std::abs (deck.getPositionSeconds() - 2.0) < 0.02, "deck: 2 s de reproduccion", deck.getPositionSeconds(), 2.0);

    // Velocidad +5 %: en 2 s avanza 2,1 s de cancion.
    deck.setPositionSeconds (0.0);
    deck.setSpeed (1.05);
    render (deck, 2.0);
    check (std::abs (deck.getPositionSeconds() - 2.1) < 0.02, "deck: velocidad 1,05", deck.getPositionSeconds(), 2.1);
    check (std::abs (deck.getCurrentBpm() - 126.0) < 1e-6, "deck: BPM efectivo con +5 %", deck.getCurrentBpm(), 126.0);
    deck.setSpeed (1.0);

    // CUE: parado fija el punto; sonando vuelve a el y para.
    deck.pause();
    deck.setPositionSeconds (1.0);
    render (deck, 0.05);
    deck.cue();
    check (std::abs (deck.getCuePoint() - 1.0) < 1e-3, "cue: parado fija el punto CUE", deck.getCuePoint(), 1.0);
    deck.play();
    render (deck, 1.0);
    deck.cue();
    render (deck, 0.05);
    check (! deck.isPlaying(), "cue: sonando para la reproduccion", deck.isPlaying() ? 1.0 : 0.0, 0.0);
    check (std::abs (deck.getPositionSeconds() - 1.0) < 1e-3, "cue: sonando vuelve al punto CUE", deck.getPositionSeconds(), 1.0);

    // Bucle de 4 pulsos: empieza en el pulso mas cercano (2,25 s) y dura 2 s.
    deck.setPositionSeconds (2.1);
    render (deck, 0.02);
    deck.setLoop (4);
    check (std::abs (deck.getLoopStart() - 2.25) < 1e-6, "bucle: empieza en el pulso cercano", deck.getLoopStart(), 2.25);
    check (std::abs (deck.getLoopEnd() - 4.25) < 1e-6, "bucle: 4 pulsos = 2 s", deck.getLoopEnd(), 4.25);
    deck.play();
    double maxPos = 0.0;
    for (int i = 0; i < 60; ++i)   // 6 s en tramos de 0,1 s
    {
        render (deck, 0.1);
        maxPos = std::max (maxPos, deck.getPositionSeconds());
    }
    check (maxPos < 4.25, "bucle: nunca pasa del final en 6 s", maxPos, 4.25);
    deck.exitLoop();
    render (deck, 3.0);
    check (deck.getPositionSeconds() > 4.25, "bucle: al salir sigue adelante", deck.getPositionSeconds(), 4.25);
}

void testSync()
{
    juce::AudioFormatManager formats;
    midj::Deck a (formats), b (formats);
    a.prepareToPlay (blockSize, sr);
    b.prepareToPlay (blockSize, sr);
    a.loadBuffer (toBuffer (clickTrack (120.0, 30.0, sr, 0.25)), sr, "a", { 120.0, 0.25 });
    b.loadBuffer (toBuffer (clickTrack (128.0, 30.0, sr, 0.50)), sr, "b", { 128.0, 0.50 });
    a.setPositionSeconds (3.1);
    b.setPositionSeconds (7.3);
    a.play();
    b.play();
    render (a, 0.1);
    render (b, 0.1);

    check (b.syncTo (a), "sync: aceptado con los dos BPM", 1.0, 1.0);
    check (std::abs (b.getSpeed() - 120.0 / 128.0) < 1e-9, "sync: velocidad = 120/128", b.getSpeed(), 120.0 / 128.0);
    // Tras 5 s sonando juntos los pulsos siguen alineados.
    for (int i = 0; i < 50; ++i) { render (a, 0.1); render (b, 0.1); }
    const double phaseError = circularDistance (a.getBeatPhase(), b.getBeatPhase());
    check (phaseError < 0.02, "sync: fases alineadas tras 5 s (pulsos)", phaseError, 0.0);
}

// --- Canal del mezclador ---------------------------------------------------------------

// Pasa un seno de 'hz' por un canal ya configurado y devuelve la ganancia en dB
// (RMS de salida / RMS de entrada), medida tras 0,5 s para que filtros y rampas se asienten.
double stripGainDb (midj::ChannelStrip& strip, double hz)
{
    strip.reset();
    const int total = (int) sr;   // 1 s
    juce::AudioBuffer<float> buf (2, total);
    for (int i = 0; i < total; ++i)
        for (int ch = 0; ch < 2; ++ch)
            buf.setSample (ch, i, (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr)));
    for (int start = 0; start < total; start += blockSize)
        strip.process (buf, start, std::min (blockSize, total - start));
    const int from = total / 2;
    const double rmsOut = buf.getRMSLevel (0, from, total - from);
    return 20.0 * std::log10 (std::max (rmsOut, 1e-9) / (0.5 / std::sqrt (2.0)));
}

void testChannelStrip()
{
    using midj::ChannelStrip;
    check (ChannelStrip::gainFromKnob (0.0f) == 0.0f, "ganancia: mando a 0 = silencio", ChannelStrip::gainFromKnob (0.0f), 0.0);
    check (ChannelStrip::gainFromKnob (0.5f) == 1.0f, "ganancia: centro = x1", ChannelStrip::gainFromKnob (0.5f), 1.0);
    check (ChannelStrip::gainFromKnob (1.0f) == 4.0f, "ganancia: maximo = x4 (+12 dB)", ChannelStrip::gainFromKnob (1.0f), 4.0);
    check (ChannelStrip::volumeFromFader (0.5f) == 0.25f, "volumen: fader a la mitad = x0,25", ChannelStrip::volumeFromFader (0.5f), 0.25);

    const auto f0 = ChannelStrip::filterFromKnob (0.0f), f1 = ChannelStrip::filterFromKnob (1.0f);
    check (! ChannelStrip::filterFromKnob (0.5f).active && ! ChannelStrip::filterFromKnob (0.45f).active
               && ! ChannelStrip::filterFromKnob (0.53f).active,
           "filtro: zona muerta 0,44-0,54", 0.0, 0.0);
    check (f0.active && f0.lowPass && std::abs (f0.cutoffHz - 20.0f) < 0.01f, "filtro: mando a 0 = paso bajo 20 Hz", f0.cutoffHz, 20.0);
    check (f1.active && ! f1.lowPass && std::abs (f1.cutoffHz - 15020.0f) < 0.5f, "filtro: mando a 1 = paso alto 15020 Hz", f1.cutoffHz, 15020.0);
    const auto fl = ChannelStrip::filterFromKnob (0.22f);   // (0,5)^3 * 15000 + 20
    check (std::abs (fl.cutoffHz - 1895.0f) < 0.5f, "filtro: curva cubica (k=0,22 -> 1895 Hz)", fl.cutoffHz, 1895.0);

    ChannelStrip strip;
    strip.prepare (sr, blockSize);

    // Todo en el centro: plano en graves, medios y agudos.
    for (double hz : { 60.0, 200.0, 1000.0, 6500.0, 12000.0 })
    {
        const double db = stripGainDb (strip, hz);
        char name[96];
        std::snprintf (name, sizeof (name), "EQ plano: %.0f Hz sin cambio (dB)", hz);
        check (std::abs (db) < 0.1, name, db, 0.0);
    }

    // Cortes de banda (kill): cada banda a 0 elimina su zona y respeta las otras.
    strip.setLow (0.0f);
    check (stripGainDb (strip, 50.0) < -30.0, "kill de graves: 50 Hz por debajo de -30 dB", stripGainDb (strip, 50.0), -30.0);
    check (std::abs (stripGainDb (strip, 1500.0)) < 0.5, "kill de graves: 1500 Hz intacto", stripGainDb (strip, 1500.0), 0.0);
    strip.setLow (0.5f);
    strip.setMid (0.0f);
    check (stripGainDb (strip, 1100.0) < -30.0, "kill de medios: 1100 Hz por debajo de -30 dB", stripGainDb (strip, 1100.0), -30.0);
    check (std::abs (stripGainDb (strip, 40.0)) < 0.5, "kill de medios: 40 Hz intacto", stripGainDb (strip, 40.0), 0.0);
    strip.setMid (0.5f);
    strip.setHigh (0.0f);
    check (stripGainDb (strip, 16000.0) < -20.0, "kill de agudos: 16 kHz por debajo de -20 dB", stripGainDb (strip, 16000.0), -20.0);
    check (std::abs (stripGainDb (strip, 300.0)) < 0.5, "kill de agudos: 300 Hz intacto", stripGainDb (strip, 300.0), 0.0);
    strip.setHigh (1.0f);
    check (std::abs (stripGainDb (strip, 16000.0) - 6.02) < 0.3, "agudos al maximo: 16 kHz +6 dB", stripGainDb (strip, 16000.0), 6.02);
    strip.setHigh (0.5f);

    // Filtro: a la izquierda quita agudos, a la derecha quita graves.
    strip.setFilter (0.2f);   // paso bajo ~1430 Hz
    check (stripGainDb (strip, 8000.0) < -20.0, "filtro paso bajo: 8 kHz atenuado", stripGainDb (strip, 8000.0), -20.0);
    check (std::abs (stripGainDb (strip, 100.0)) < 0.5, "filtro paso bajo: 100 Hz intacto", stripGainDb (strip, 100.0), 0.0);
    strip.setFilter (0.8f);   // paso alto ~2730 Hz
    check (stripGainDb (strip, 200.0) < -30.0, "filtro paso alto: 200 Hz atenuado", stripGainDb (strip, 200.0), -30.0);
    check (std::abs (stripGainDb (strip, 15000.0)) < 0.5, "filtro paso alto: 15 kHz intacto", stripGainDb (strip, 15000.0), 0.0);
    strip.setFilter (0.5f);

    // Ganancia + volumen: +12 dB y fader a la mitad (-12 dB) se compensan.
    strip.setGain (1.0f);
    strip.setVolume (0.5f);
    check (std::abs (stripGainDb (strip, 1000.0)) < 0.1, "ganancia x4 y volumen x0,25 = 0 dB", stripGainDb (strip, 1000.0), 0.0);
    strip.setGain (0.5f);
    strip.setVolume (1.0f);

    // Sin clics: bajar el volumen de golpe con una senal continua produce una rampa suave.
    strip.reset();
    juce::AudioBuffer<float> dc (2, blockSize * 8);
    dc.clear();
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.5f, dc.getNumSamples());
    strip.process (dc, 0, blockSize * 4);   // se asienta con volumen 1
    strip.setVolume (0.0f);
    strip.process (dc, blockSize * 4, blockSize * 4);
    float maxStep = 0.0f;
    for (int i = blockSize * 4 + 1; i < dc.getNumSamples(); ++i)
        maxStep = std::max (maxStep, std::abs (dc.getSample (0, i) - dc.getSample (0, i - 1)));
    check (maxStep < 0.005f, "volumen a 0 de golpe: sin saltos (rampa de 20 ms)", maxStep, 0.0);
    check (std::abs (dc.getSample (0, dc.getNumSamples() - 1)) < 1e-4f, "volumen a 0: termina en silencio",
           dc.getSample (0, dc.getNumSamples() - 1), 0.0);
}

// Cadena completa deck -> canal -> crossfader: el canal de cada deck solo afecta a ese deck.
void testMixerChannels()
{
    juce::AudioFormatManager formats;
    midj::Deck a (formats), b (formats);
    midj::Mixer mixer (a, b);
    mixer.prepareToPlay (blockSize, sr);

    juce::AudioBuffer<float> bass (1, (int) (sr * 10.0));
    for (int i = 0; i < bass.getNumSamples(); ++i)
        bass.setSample (0, i, (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 50.0 * i / sr)));
    a.loadBuffer (bass, sr, "graves", { 120.0, 0.0 });
    b.loadBuffer (bass, sr, "graves", { 120.0, 0.0 });
    a.play();
    b.play();

    // Devuelve el RMS de la salida del mezclador en el ultimo cuarto de 1 s de audio.
    auto outputRms = [&]
    {
        juce::AudioBuffer<float> out (2, blockSize);
        double sum = 0.0;
        int count = 0;
        const int blocks = (int) sr / blockSize;
        for (int k = 0; k < blocks; ++k)
        {
            juce::AudioSourceChannelInfo info (&out, 0, blockSize);
            mixer.getNextAudioBlock (info);
            if (k >= blocks * 3 / 4)
                for (int i = 0; i < blockSize; ++i, ++count)
                    sum += (double) out.getSample (0, i) * out.getSample (0, i);
        }
        return std::sqrt (sum / count);
    };

    mixer.setCrossfader (0.0f);   // solo se oye A
    const double base = outputRms();
    check (base > 0.3, "mezclador: deck A suena con el crossfader a la izquierda", base, 0.35);
    mixer.getStrip (1).setLow (0.0f);   // tocar el canal B no debe cambiar A
    check (std::abs (outputRms() - base) < 0.005, "mezclador: el EQ de B no afecta a A", outputRms(), base);
    mixer.getStrip (0).setLow (0.0f);
    const double killed = outputRms();
    check (killed < base * 0.03, "mezclador: kill de graves en A corta su bajo", killed, 0.0);
}

// --- Keylock (cambio de tempo sin cambio de tono) ----------------------------------------

// Reproduce 'seconds' y devuelve la salida estereo. Si se pide, apunta la posicion de la
// cancion (segundos) al principio de cada bloque para saber que se oia en cada momento.
juce::AudioBuffer<float> renderOutput (midj::Deck& deck, double seconds, std::vector<double>* positions = nullptr)
{
    const int blocks = (int) (seconds * sr) / blockSize;
    juce::AudioBuffer<float> out (2, blocks * blockSize);
    for (int k = 0; k < blocks; ++k)
    {
        if (positions != nullptr)
            positions->push_back (deck.getPositionSeconds());
        juce::AudioSourceChannelInfo info (&out, k * blockSize, blockSize);
        deck.getNextAudioBlock (info);
    }
    return out;
}

// Frecuencia de un seno por cruces por cero (con interpolacion), en [from, to) muestras.
double sineFrequency (const juce::AudioBuffer<float>& b, int from, int to)
{
    const float* d = b.getReadPointer (0);
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (int i = from + 1; i < to; ++i)
        if (d[i - 1] < 0.0f && d[i] >= 0.0f)
        {
            const double t = (i - 1) + d[i - 1] / (d[i - 1] - d[i]);
            if (first < 0.0) first = t;
            last = t;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * sr / (last - first) : 0.0;
}

// Cuanto se ha desplazado en frecuencia 'processed' respecto a 'original': compara sus
// espectros medios en escala logaritmica y busca el factor que mejor los alinea.
// 1.00 = mismo tono; 1.08 = un 8 % mas agudo (lo que hace un vinilo a +8 %).
double spectralShift (const juce::AudioBuffer<float>& original, const juce::AudioBuffer<float>& processed, double rate)
{
    constexpr int order = 13, size = 1 << order;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> window ((size_t) size, juce::dsp::WindowingFunction<float>::hann, false);
    auto spectrum = [&] (const juce::AudioBuffer<float>& b)
    {
        std::vector<double> mag (size / 2, 0.0);
        std::vector<float> frame (size * 2);
        for (int start = 0; start + size <= b.getNumSamples(); start += size / 2)
        {
            std::fill (frame.begin(), frame.end(), 0.0f);
            for (int i = 0; i < size; ++i)
                frame[(size_t) i] = 0.5f * (b.getSample (0, start + i) + b.getSample (b.getNumChannels() - 1, start + i));
            window.multiplyWithWindowingTable (frame.data(), (size_t) size);
            fft.performFrequencyOnlyForwardTransform (frame.data());
            for (int i = 0; i < size / 2; ++i)
                mag[(size_t) i] += frame[(size_t) i];
        }
        for (auto& m : mag)
            m = std::log (m + 1e-6);
        return mag;
    };
    const auto a = spectrum (original), b = spectrum (processed);
    auto at = [] (const std::vector<double>& s, double bin)
    {
        const int i = (int) bin;
        const double t = bin - i;
        return s[(size_t) i] * (1.0 - t) + s[(size_t) i + 1] * t;
    };
    const double binHz = rate / size;
    double bestScale = 1.0, bestScore = -1e300;
    for (double scale = 0.90; scale <= 1.15; scale += 0.001)
    {
        // Correlacion entre A(f) y B(f * scale) entre 150 y 4000 Hz.
        double sa = 0, sb = 0, sab = 0, saa = 0, sbb = 0;
        int count = 0;
        for (double f = 150.0; f < 4000.0; f *= 1.002, ++count)
        {
            const double x = at (a, f / binHz), y = at (b, f * scale / binHz);
            sa += x; sb += y; sab += x * y; saa += x * x; sbb += y * y;
        }
        const double cov = sab - sa * sb / count;
        const double score = cov / std::sqrt ((saa - sa * sa / count) * (sbb - sb * sb / count));
        if (score > bestScore) { bestScore = score; bestScale = scale; }
    }
    return bestScale;
}

void testKeyLock()
{
    juce::AudioFormatManager formats;
    midj::Deck deck (formats);
    deck.prepareToPlay (blockSize, sr);
    check (deck.isKeyLocked(), "keylock activado por defecto", 1.0, 1.0);

    // 1. El tono: un La de 440 Hz a +8 %. Con keylock sigue en 440; sin keylock sube a 475,2.
    juce::AudioBuffer<float> tone (1, (int) (sr * 20.0));
    for (int i = 0; i < tone.getNumSamples(); ++i)
        tone.setSample (0, i, (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr)));
    deck.loadBuffer (tone, sr, "la440", { 120.0, 0.0 });
    deck.setSpeed (1.08);
    deck.play();
    auto out = renderOutput (deck, 3.0);
    const double locked = sineFrequency (out, (int) sr, out.getNumSamples());
    check (std::abs (locked - 440.0) < 0.5, "keylock +8 %: el La sigue en 440 Hz", locked, 440.0);
    check (std::abs (deck.getPositionSeconds() - 3.0 * 1.08) < 0.03, "keylock +8 %: la cancion avanza un 8 % mas",
           deck.getPositionSeconds(), 3.24);

    deck.setKeyLock (false);
    out = renderOutput (deck, 2.0);
    const double vinyl = sineFrequency (out, (int) (sr * 0.5), out.getNumSamples());
    check (std::abs (vinyl - 475.2) < 0.5, "sin keylock +8 %: el La sube a 475,2 Hz", vinyl, 475.2);

    // Se puede volver a activar con la cancion sonando.
    deck.setKeyLock (true);
    out = renderOutput (deck, 2.0);
    const double relocked = sineFrequency (out, (int) (sr * 0.5), out.getNumSamples());
    check (std::abs (relocked - 440.0) < 0.5, "keylock reactivado sonando: vuelve a 440 Hz", relocked, 440.0);

    // 2. El tempo y la fase: bombos a 120 BPM tocados a +10 % con keylock.
    //    La salida debe ir a 132 BPM y sus golpes caer donde el deck dice que esta (SYNC y
    //    bucles dependen de eso): se mide la fase de cada golpe oido en la rejilla de la cancion.
    deck.loadBuffer (toBuffer (clickTrack (120.0, 40.0, sr, 0.25)), sr, "clicks120", { 120.0, 0.25 });
    deck.setSpeed (1.10);
    deck.setPositionSeconds (2.0);
    deck.play();
    std::vector<double> positions;
    out = renderOutput (deck, 16.0, &positions);
    const auto heard = midj::BpmDetector::analyse (out, sr);
    check (std::abs (heard.bpm - 132.0) < 0.05, "keylock +10 %: los bombos suenan a 132 BPM", heard.bpm, 132.0);

    double worst = 0.0;
    for (double t = heard.firstBeat; t < 15.0; t += 60.0 / heard.bpm)
    {
        if (t < 2.0)
            continue;   // se deja asentar
        const int block = (int) (t * sr) / blockSize;
        const double offset = t - block * blockSize / sr;
        const double songTime = positions[(size_t) block] + offset * 1.10;
        const double beats = (songTime - 0.25) * 120.0 / 60.0;
        worst = std::max (worst, std::abs (beats - std::round (beats)));
    }
    check (worst < 0.02, "keylock: lo que se oye coincide con la posicion (pulsos)", worst, 0.0);

    // 3. Un salto (CUE) con keylock empieza en el sitio exacto: el golpe en 2,25 s suena
    //    10 ms (de cancion) despues del salto, es decir 9,1 ms reales a +10 %.
    deck.setPositionSeconds (2.25 - 0.01);
    out = renderOutput (deck, 0.2);
    int onset = -1;
    for (int i = 0; i < out.getNumSamples() && onset < 0; ++i)
        if (std::abs (out.getSample (0, i)) > 0.1f)
            onset = i;
    const double onsetMs = onset * 1000.0 / sr;
    check (onset >= 0 && std::abs (onsetMs - 10.0 / 1.10) < 3.0, "keylock: tras un salto el golpe llega a tiempo (ms)",
           onsetMs, 10.0 / 1.10);
}

// Coste de CPU del keylock: tiempo de calculo por segundo de audio (por deck).
void benchmarkKeyLock (midj::TimeStretcher::Engine engine)
{
    juce::AudioFormatManager formats;
    midj::Deck deck (formats);
    deck.setKeyLockEngine (engine);
    deck.prepareToPlay (blockSize, sr);
    juce::Random rng (1);
    juce::AudioBuffer<float> noise (2, (int) (sr * 30.0));
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < noise.getNumSamples(); ++i)
            noise.setSample (ch, i, rng.nextFloat() * 0.4f - 0.2f);
    deck.loadBuffer (noise, sr, "ruido", { 120.0, 0.0 });
    deck.setSpeed (1.06);
    deck.play();
    const auto start = juce::Time::getMillisecondCounterHiRes();
    renderOutput (deck, 10.0);
    const double load = (juce::Time::getMillisecondCounterHiRes() - start) / 10000.0;
    std::printf ("[INFO]  keylock %s: %.1f %% de un nucleo por deck\n",
                 engine == midj::TimeStretcher::Engine::finer ? "R3 (finer) " : "R2 (faster)", load * 100.0);
}

// --- Herramientas de linea de comandos para probar con canciones reales ---------------

std::unique_ptr<juce::AudioBuffer<float>> readFile (juce::AudioFormatManager& formats, const juce::File& file, double& rate)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return nullptr;
    rate = reader->sampleRate;
    const int length = (int) reader->lengthInSamples;
    auto b = std::make_unique<juce::AudioBuffer<float>> (2, length);
    reader->read (b.get(), 0, length, 0, true, true);
    return b;
}

int analyseFiles (const juce::StringArray& paths)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (const auto& p : paths)
    {
        double rate = 0.0;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        auto audio = readFile (formats, juce::File (p), rate);
        if (audio == nullptr)
        {
            std::printf ("No se pudo abrir: %s\n", p.toRawUTF8());
            continue;
        }
        const auto a = midj::BpmDetector::analyse (*audio, rate);
        std::printf ("%7.2f BPM  primer pulso %.3f s  (%.3f s de audio a %.0f Hz, %.0f ms)  %s\n", a.bpm, a.firstBeat,
                     audio->getNumSamples() / rate, rate, juce::Time::getMillisecondCounterHiRes() - start,
                     juce::File (p).getFileName().toRawUTF8());
    }
    return 0;
}

// Toca 30 s desde la mitad de la cancion a 'speed' con y sin keylock y mide el resultado.
// Guarda los WAV en 'outDir' para escucharlos.
int stretchFile (const juce::String& path, double speed, const juce::File& outDir)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    double rate = 0.0;
    auto audio = readFile (formats, juce::File (path), rate);
    if (audio == nullptr)
        return 1;
    const auto analysis = midj::BpmDetector::analyse (*audio, rate);
    const double from = audio->getNumSamples() / rate / 2.0, seconds = 30.0;
    juce::AudioBuffer<float> original (2, (int) (seconds * rate));
    for (int ch = 0; ch < 2; ++ch)
        original.copyFrom (ch, 0, *audio, ch, (int) (from * rate), original.getNumSamples());
    const auto originalBpm = midj::BpmDetector::analyse (original, rate).bpm;

    std::printf ("%s\n  original:                %.2f BPM (cancion entera %.2f)\n",
                 juce::File (path).getFileName().toRawUTF8(), originalBpm, analysis.bpm);
    for (bool lock : { true, false })
    {
        midj::Deck deck (formats);
        deck.prepareToPlay (blockSize, rate);
        deck.loadBuffer (*audio, rate, "cancion", analysis);
        deck.setKeyLock (lock);
        deck.setSpeed (speed);
        deck.setPositionSeconds (from);
        deck.play();
        juce::AudioBuffer<float> out (2, (int) (seconds / speed * rate) / blockSize * blockSize);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int k = 0; k < out.getNumSamples() / blockSize; ++k)
        {
            juce::AudioSourceChannelInfo info (&out, k * blockSize, blockSize);
            deck.getNextAudioBlock (info);
        }
        const double cpu = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0 / (out.getNumSamples() / rate);
        const auto heard = midj::BpmDetector::analyse (out, rate);
        const double shift = spectralShift (original, out, rate);
        std::printf ("  %-12s a x%.3f: %.2f BPM (esperado %.2f), tono x%.3f (%+.2f semitonos), CPU %.1f %%\n",
                     lock ? "con keylock" : "sin keylock", speed, heard.bpm, originalBpm * speed, shift,
                     12.0 * std::log2 (shift), cpu * 100.0);

        const auto file = outDir.getChildFile (juce::File (path).getFileNameWithoutExtension().substring (0, 24).trim()
                                               + (lock ? " keylock x" : " vinilo x") + juce::String (speed, 2) + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), rate, 2, 16, {}, 0));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    }
    return 0;
}

}

#if JUCE_WINDOWS
int wmain (int argc, wchar_t* argv[])   // argumentos en Unicode: nombres con acentos
#else
int main (int argc, char* argv[])
#endif
{
    const juce::ScopedJuceInitialiser_GUI juce;   // los decks avisan a la interfaz con mensajes

    // Herramientas para canciones reales (no forman parte de ctest):
    //   MiDJTests --analyse a.mp3 b.mp3 ...           BPM de cada archivo
    //   MiDJTests --stretch cancion.mp3 1.08 carpeta   keylock: BPM, tono y WAV para escuchar
    //   MiDJTests --benchmark                          CPU del keylock
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String (argv[i]));
    if (args[0] == "--analyse")
        return analyseFiles (juce::StringArray (args.begin() + 1, args.size() - 1));
    if (args[0] == "--stretch" && args.size() >= 4)
        return stretchFile (args[1], args[2].getDoubleValue(), juce::File (args[3]));
    if (args[0] == "--compare" && args.size() >= 3)   // dos grabaciones: BPM de cada una y tono de B respecto de A
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        double rateA = 0.0, rateB = 0.0;
        auto a = readFile (formats, juce::File (args[1]), rateA);
        auto b = readFile (formats, juce::File (args[2]), rateB);
        if (a == nullptr || b == nullptr)
            return 1;
        const double shift = spectralShift (*a, *b, rateA);
        std::printf ("A: %.2f BPM   B: %.2f BPM   tono de B = x%.3f de A (%+.2f semitonos)\n",
                     midj::BpmDetector::analyse (*a, rateA).bpm, midj::BpmDetector::analyse (*b, rateB).bpm,
                     shift, 12.0 * std::log2 (shift));
        return 0;
    }
    if (args[0] == "--benchmark")
    {
        benchmarkKeyLock (midj::TimeStretcher::Engine::finer);
        benchmarkKeyLock (midj::TimeStretcher::Engine::faster);
        return 0;
    }

    std::printf ("== Detector de BPM ==\n");
    testBpm (120.0, 44100.0, 120.0);
    testBpm (128.0, 44100.0, 128.0);
    testBpm (95.0, 48000.0, 95.0);
    testBpm (174.0, 44100.0, 87.0);    // tambor y bajo: se pliega a la mitad (rango 85-170)
    testBpm (70.0, 44100.0, 140.0);    // muy lento: se dobla
    std::vector<float> silence (44100 * 10, 0.0f);
    check (midj::BpmDetector::detect (silence.data(), (int) silence.size(), 44100.0) == 0.0,
           "silencio devuelve 0", 0.0, 0.0);
    testPhase (120.0, 0.25);
    testPhase (128.0, 0.50);
    testPhase (100.0, 0.10);
    testDembow (88.0, 0.14);
    testDembow (95.0, 0.30);

    std::printf ("== Crossfader (curva smooth de VirtualDJ) ==\n");
    using midj::Mixer;
    check (Mixer::leftGain (0.0f) == 1.0f, "izquierda a tope en x=0", Mixer::leftGain (0.0f), 1.0);
    check (std::abs (Mixer::leftGain (0.5f) - 0.7496f) < 1e-3, "izquierda en el centro", Mixer::leftGain (0.5f), 0.7496);
    check (Mixer::leftGain (1.0f) == 0.0f, "izquierda en silencio en x=1", Mixer::leftGain (1.0f), 0.0);
    check (Mixer::rightGain (0.0f) == 0.0f, "derecha en silencio en x=0", Mixer::rightGain (0.0f), 0.0);
    check (Mixer::rightGain (0.2f) == Mixer::leftGain (0.8f), "simetria", Mixer::rightGain (0.2f), Mixer::leftGain (0.8f));

    std::printf ("== Canal: ganancia, EQ de 3 bandas, filtro y volumen ==\n");
    testChannelStrip();
    testMixerChannels();

    std::printf ("== Keylock: tempo sin cambiar el tono ==\n");
    testKeyLock();

    std::printf ("== Deck: reproduccion, CUE y bucles ==\n");
    testDeck();
    std::printf ("== SYNC ==\n");
    testSync();

    std::printf ("\n%s (%d fallos)\n", failures == 0 ? "TODO OK" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}
