// Pruebas automaticas del motor. Se ejecutan con: ctest --preset debug
// Devuelven codigo 0 si todo pasa y 1 si algo falla. No necesitan tarjeta de sonido:
// el audio se "reproduce" pidiendo bloques al deck directamente.
#include "Control/ControlSurface.h"
#include "Control/MidiController.h"
#include "Control/MidiMapping.h"
#include "Engine/Automix.h"
#include "Engine/BpmDetector.h"
#include "Engine/MixPoints.h"
#include "Engine/ChannelStrip.h"
#include "Engine/Deck.h"
#include "Engine/Effects.h"
#include "Engine/HeadphoneOutput.h"
#include "Video/YouTubeSearch.h"
#include "Video/AlphaVideo.h"
#include "Engine/Mixer.h"
#include "Engine/Sampler.h"
#include "Engine/StemSeparator.h"
#include "Engine/StemsService.h"
#include "Library/Library.h"
#include "Video/Capture.h"
#include "Video/Compositor.h"
#include "Video/DeckVideo.h"
#include "Video/VideoMix.h"
#include "Video/VideoProgram.h"
#include "Video/Visualizer.h"
#include "Video/MediaSources.h"
#include "Video/MediaFoundation.h"
#include "Library/TagReader.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <cmath>
#include <functional>
#include <mutex>
#include <thread>
#include <map>
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

double wrapPhase (double x)   // desfase de fase de pulso llevado a -0,5..0,5
{
    x -= std::floor (x);
    return x > 0.5 ? x - 1.0 : x;
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
// separacion (3/4 de pulso) engaÃ±a a un detector que solo mira la energia total: da 4/3 del tempo.
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
    formats.registerFormat (new midj::MediaFoundationAudioFormat(), false);   // MP4, M4A...
    for (const auto& p : paths)
    {
        double rate = 0.0;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        auto audio = readFile (formats, juce::File (p), rate);
        const double readMs = juce::Time::getMillisecondCounterHiRes() - start;
        if (audio == nullptr)
        {
            std::printf ("No se pudo abrir: %s\n", p.toRawUTF8());
            continue;
        }
        const auto a = midj::BpmDetector::analyse (*audio, rate);
        std::printf ("%7.2f BPM  primer pulso %.3f s  (%.3f s de audio a %.0f Hz, lectura %.0f ms, total %.0f ms)  %s\n", a.bpm, a.firstBeat,
                     audio->getNumSamples() / rate, rate, readMs, juce::Time::getMillisecondCounterHiRes() - start,
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
    formats.registerFormat (new midj::MediaFoundationAudioFormat(), false);   // MP4, M4A...
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


// --- Biblioteca (SQLite) -----------------------------------------------------------------

// Escribe un WAV de 'seconds' con bombos a 'bpm'.
void writeClickWav (const juce::File& file, double bpm, double seconds = 20.0)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    const auto clicks = clickTrack (bpm, seconds, sr, 0.25);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), sr, 1, 16, {}, 0));
    const float* data = clicks.data();
    writer->writeFromFloatArrays (&data, 1, (int) clicks.size());
}

void pumpMessages();

// Play mientras el deck carga (en un equipo lento tarda): suena al terminar; Pausa lo anula.
void testDeckPlayWhileLoading()
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    midj::Deck deck (formats);
    deck.prepareToPlay (blockSize, sr);
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsLoad.wav");
    writeClickWav (file, 120.0);
    auto waitLoaded = [&deck]
    {
        for (int i = 0; i < 300 && deck.isLoading(); ++i)
        {
            pumpMessages();   // la carga termina con un mensaje al hilo de interfaz
            juce::Thread::sleep (20);
        }
    };

    deck.load (file);
    const bool wasLoading = deck.isLoading();
    deck.play();
    const bool pendingWhileLoading = deck.isPlayPending() && ! deck.isPlaying();
    waitLoaded();
    check (wasLoading && pendingWhileLoading && deck.hasTrack() && deck.isPlaying() && ! deck.isPlayPending(),
           "deck: Play durante la carga suena al terminar", deck.isPlaying() ? 1.0 : 0.0, 1.0);

    deck.load (file);
    deck.play();
    deck.togglePlay();   // Pausa antes de que termine
    waitLoaded();
    check (deck.hasTrack() && ! deck.isPlaying() && ! deck.isPlayPending(), "deck: Pausa durante la carga lo anula",
           deck.isPlaying() ? 1.0 : 0.0, 0.0);
    file.deleteFile();
}

void testTagReader()
{
    using namespace midj::TagReader;

    // ID3v2.3: titulo en Latin-1 (con o acentuada), artista en UTF-16 con BOM, genero "(17)Rock".
    juce::MemoryOutputStream frames;
    auto frame23 = [&frames] (const char* id, const juce::MemoryBlock& body)
    {
        frames.write (id, 4);
        const auto n = (uint32_t) body.getSize();
        const uint8_t size[4] = { (uint8_t) (n >> 24), (uint8_t) (n >> 16), (uint8_t) (n >> 8), (uint8_t) n };
        frames.write (size, 4);
        frames.writeShort (0);
        frames.write (body.getData(), body.getSize());
    };
    const uint8_t title[] = { 0, 'C', 'a', 'n', 'c', 'i', 0xf3, 'n' };
    const uint8_t artist[] = { 1, 0xff, 0xfe, 0xd1, 0, 'a', 0, 'n', 0, 'd', 0, 0xfa, 0, 0, 0 };   // "Ã‘andÃº"
    const uint8_t genre[] = { 0, '(', '1', '7', ')', 'R', 'o', 'c', 'k' };
    const uint8_t bpmText[] = { 0, '1', '2', '8' };
    frame23 ("TIT2", juce::MemoryBlock (title, sizeof (title)));
    frame23 ("TPE1", juce::MemoryBlock (artist, sizeof (artist)));
    frame23 ("TCON", juce::MemoryBlock (genre, sizeof (genre)));
    frame23 ("TBPM", juce::MemoryBlock (bpmText, sizeof (bpmText)));
    juce::MemoryOutputStream tag;
    const auto n = (uint32_t) frames.getDataSize();
    const uint8_t header[10] = { 'I', 'D', '3', 3, 0, 0, (uint8_t) ((n >> 21) & 0x7f), (uint8_t) ((n >> 14) & 0x7f),
                                 (uint8_t) ((n >> 7) & 0x7f), (uint8_t) (n & 0x7f) };
    tag.write (header, 10);
    tag.write (frames.getData(), frames.getDataSize());
    const auto t23 = parseId3v2 (tag.getData(), tag.getDataSize());
    check (t23.title == juce::String (juce::CharPointer_UTF8 ("Canci\xc3\xb3n")), "ID3v2.3: titulo Latin-1", t23.title.length(), 7);
    check (t23.artist == juce::String (juce::CharPointer_UTF8 ("\xc3\x91" "and\xc3\xba")), "ID3v2.3: artista UTF-16", t23.artist.length(), 5);
    check (t23.genre == "Rock", "ID3v2.3: genero '(17)Rock' -> Rock", t23.genre.length(), 4);
    check (t23.bpm == 128.0, "ID3v2.3: BPM de la etiqueta", t23.bpm, 128.0);

    // ID3v2.4: tamanos "syncsafe" tambien en los marcos y texto UTF-8.
    const uint8_t v24[] = { 'I', 'D', '3', 4, 0, 0, 0, 0, 0, 19,
                            'T', 'I', 'T', '2', 0, 0, 0, 9, 0, 0, 3, 'B', 'a', 'i', 'l', 0xc3, 0xa9, 'm', 'o' };
    const auto t24 = parseId3v2 (v24, sizeof (v24));
    check (t24.title == juce::String (juce::CharPointer_UTF8 ("Bail\xc3\xa9mo")), "ID3v2.4: titulo UTF-8", t24.title.length(), 7);

    // ID3v2.2: identificadores de 3 letras.
    const uint8_t v22[] = { 'I', 'D', '3', 2, 0, 0, 0, 0, 0, 10, 'T', 'P', '1', 0, 0, 4, 0, 'D', 'J', 'X' };
    check (parseId3v2 (v22, sizeof (v22)).artist == "DJX", "ID3v2.2: artista", 0.0, 0.0);

    // ID3v1 (128 bytes al final del archivo).
    uint8_t v1[128] = { 'T', 'A', 'G' };
    std::memcpy (v1 + 3, "Viejo Tema", 10);
    std::memcpy (v1 + 33, "Grupo Antiguo", 13);
    const auto t1 = parseId3v1 (v1, sizeof (v1));
    check (t1.title == "Viejo Tema" && t1.artist == "Grupo Antiguo", "ID3v1: titulo y artista", 0.0, 0.0);

    // Sin etiquetas: "Artista - Titulo" en el nombre; si no hay guion, todo es titulo.
    midj::TrackTags fromName;
    fillFromFileName (fromName, "Luister La Voz - La Maria - Remix");
    check (fromName.artist == "Luister La Voz" && fromName.title == "La Maria - Remix", "nombre: 'Artista - Titulo'", 0.0, 0.0);
    midj::TrackTags onlyTitle;
    fillFromFileName (onlyTitle, "pista suelta");
    check (onlyTitle.artist.isEmpty() && onlyTitle.title == "pista suelta", "nombre sin guion = titulo", 0.0, 0.0);
}

void testLibrary()
{
    using midj::Library;
    check (Library::normalise (juce::String (juce::CharPointer_UTF8 ("Mar\xc3\xad" "a \xc3\x91" "and\xc3\xba \xc3\x89XITO")))
               == "maria nandu exito", "normalizar: minusculas y sin acentos", 0.0, 0.0);

    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsLibrary");
    root.deleteRecursively();
    const auto music = root.getChildFile ("Musica");
    const auto maria = music.getChildFile (juce::String (juce::CharPointer_UTF8 ("Luister La Voz - La Mar\xc3\xad" "a.wav")));
    const auto slow = music.getChildFile ("Otro Artista - Cancion Lenta.wav");
    const auto loose = music.getChildFile ("solo titulo.wav");
    const auto deep = music.getChildFile ("sub").getChildFile ("Deep - Uno.wav");
    writeClickWav (maria, 120.0);
    writeClickWav (slow, 100.0);
    writeClickWav (loose, 110.0);
    writeClickWav (deep, 128.0);
    music.getChildFile ("notas.txt").replaceWithText ("no es audio");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const auto dbFile = root.getChildFile ("library.db");
    {
        Library library (formats);
        check (library.open (dbFile), "biblioteca: crear la base de datos", 1.0, 1.0);
        check (library.getDurationLimits().minAudio == 60.0, "duracion: por defecto al menos 1 minuto", library.getDurationLimits().minAudio, 60.0);
        library.setDurationLimits ({});   // los archivos de prueba duran 20 s
        library.addFolder (music);
        library.scanNow();
        check (library.getNumTracks() == 4, "escaneo: 4 canciones (subcarpetas si, .txt no)", library.getNumTracks(), 4);

        const auto t = library.findByFile (maria);
        check (t.has_value() && t->artist == "Luister La Voz"
                   && t->title == juce::String (juce::CharPointer_UTF8 ("La Mar\xc3\xad" "a")),
               "escaneo: artista y titulo del nombre", 0.0, 0.0);
        check (t.has_value() && std::abs (t->duration - 20.0) < 0.01, "escaneo: duracion", t ? t->duration : 0.0, 20.0);
        check (t.has_value() && ! t->analysed, "escaneo: pendiente de analizar", 0.0, 0.0);

        check (library.search ("maria").size() == 1, "buscar 'maria' encuentra 'Maria' con acento",
               (double) library.search ("maria").size(), 1.0);
        check (library.search (juce::String (juce::CharPointer_UTF8 ("LUISTER mar\xc3\xad" "a"))).size() == 1,
               "buscar varias palabras, mayusculas", (double) library.search ("LUISTER maria").size(), 1.0);
        check (library.search ("cancion artista").size() == 1, "buscar palabras en distinto orden",
               (double) library.search ("cancion artista").size(), 1.0);
        check (library.search ("zzz").empty(), "buscar algo que no esta: nada", (double) library.search ("zzz").size(), 0.0);
        check (library.search ("").size() == 4, "busqueda vacia: todo", (double) library.search ("").size(), 4.0);
        check (library.search ("", music.getChildFile ("sub")).size() == 1, "filtrar por carpeta",
               (double) library.search ("", music.getChildFile ("sub")).size(), 1.0);
        check (library.search ("100%").empty() && library.search ("_").empty(), "buscar % y _ no son comodines", 0.0, 0.0);

        auto beforeAnalysis = library.search ("", {}, Library::SortColumn::title, true);
        const auto contentBefore = library.getContentVersion();
        check (library.analysePendingNow() == 4, "analisis: 4 pendientes", 4.0, 4.0);
        // Un BPM analizado no cambia el contenido (la lista no se rehace), pero se puede releer.
        check (library.getContentVersion() == contentBefore, "analisis: no cambia la version del contenido",
               (double) library.getContentVersion(), (double) contentBefore);
        check (! beforeAnalysis.empty() && library.updateAnalysis (beforeAnalysis.front()) && beforeAnalysis.front().bpm > 0.0
                   && ! library.updateAnalysis (beforeAnalysis.front()),
               "updateAnalysis: trae el BPM nuevo una vez", beforeAnalysis.empty() ? 0.0 : beforeAnalysis.front().bpm, 1.0);
        const auto bpmOf = [&] (const juce::File& f) { auto x = library.findByFile (f); return x ? x->bpm : 0.0; };
        check (std::abs (bpmOf (maria) - 120.0) < 0.05 && std::abs (bpmOf (slow) - 100.0) < 0.05
                   && std::abs (bpmOf (deep) - 128.0) < 0.05,
               "analisis: 120, 100 y 128 BPM guardados", bpmOf (deep), 128.0);

        // El deck y la biblioteca deben dar exactamente el mismo resultado.
        {
            double rate = 0.0;
            auto audio = readFile (formats, maria, rate);
            const auto deckAnalysis = midj::BpmDetector::analyse (*audio, rate);
            const auto stored = library.findByFile (maria);
            check (stored && stored->bpm == deckAnalysis.bpm && stored->firstBeat == deckAnalysis.firstBeat,
                   "analisis: identico al del deck", stored ? stored->bpm : 0.0, deckAnalysis.bpm);
        }

        const auto byBpm = library.search ("", {}, Library::SortColumn::bpm, false);
        check (byBpm.size() == 4 && byBpm.front().bpm > byBpm.back().bpm, "ordenar por BPM descendente",
               byBpm.empty() ? 0.0 : byBpm.front().bpm, 128.0);

        // Volver a escanear sin cambios no repite nada.
        library.scanNow();
        check (library.analysePendingNow() == 0, "re-escanear sin cambios: nada que analizar", 0.0, 0.0);

        // Un archivo cambiado se vuelve a analizar; uno borrado desaparece.
        writeClickWav (slow, 90.0, 25.0);
        slow.setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::minutes (1));
        loose.deleteFile();
        const auto contentBeforeScan = library.getContentVersion();
        library.scanNow();
        check (library.getContentVersion() != contentBeforeScan, "escanear con cambios: sube la version del contenido",
               (double) library.getContentVersion(), (double) contentBeforeScan + 1.0);
        check (library.getNumTracks() == 3, "archivo borrado: sale de la biblioteca", library.getNumTracks(), 3);
        const int reanalysed = library.analysePendingNow();
        check (reanalysed == 1 && std::abs (bpmOf (slow) - 90.0) < 0.05,
               "archivo cambiado: se re-analiza (90 BPM)", bpmOf (slow), 90.0);

        // Analisis guardado desde fuera (lo que hace un deck al cargar un archivo nuevo).
        library.setAnalysis (deep, { 127.5, 0.1 });
        check (bpmOf (deep) == 127.5, "guardar analisis de un deck", bpmOf (deep), 127.5);
    }
    {
        // Lo guardado sigue ahi al volver a abrir.
        Library library (formats);
        library.open (dbFile);
        check (library.getNumTracks() == 3 && library.getFolders().size() == 1, "reabrir: canciones y carpetas guardadas",
               library.getNumTracks(), 3);
        const auto t = library.findByFile (maria);
        check (t && t->analysed && std::abs (t->bpm - 120.0) < 0.05, "reabrir: el BPM no se pierde", t ? t->bpm : 0.0, 120.0);
        // Quitar una carpeta del arbol que esta dentro de otra: no vuelve al re-escanear; Deshacer la
        // trae con su analisis.
        const auto sub = music.getChildFile ("sub");
        auto removal = library.removeFromLibrary (sub);
        library.scanNow();
        check (library.getNumTracks() == 2 && library.search ("", sub).empty() && library.getFolders().size() == 1,
               "arbol: quitar una subcarpeta (no vuelve al re-escanear)", library.getNumTracks(), 2);
        library.undoRemoval (removal);
        const auto back = library.findByFile (deep);
        check (library.getNumTracks() == 3 && back && back->analysed, "arbol: deshacer la trae con su analisis", library.getNumTracks(), 3);
        // Quitar el disco entero: tambien deja de ser carpeta de la biblioteca; deshacer la recupera.
        removal = library.removeFromLibrary (root);
        check (library.getNumTracks() == 0 && library.getFolders().isEmpty(), "arbol: quitar un disco quita sus carpetas", library.getNumTracks(), 0);
        library.undoRemoval (removal);
        check (library.getNumTracks() == 3 && library.getFolders().size() == 1, "arbol: deshacer quitar un disco", library.getNumTracks(), 3);

        // Sincronizar con el PC: encuentra las carpetas con audio (sin repetir subcarpetas).
        int files = 0;
        Library::SyncOptions sync;
        sync.roots.add (root);
        const auto found = Library::findMediaFolders (sync, nullptr, [] { return false; }, files);
        check (found.size() == 1 && found[0] == music && files == 3, "sincronizar: carpetas con musica", (double) files, 3.0);
        sync.audio = false;
        check (Library::findMediaFolders (sync, nullptr, [] { return false; }, files).isEmpty(), "sincronizar: solo video (no hay)", 0.0, 0.0);

        library.removeFolder (music);
        check (library.getNumTracks() == 0 && library.getFolders().isEmpty(), "quitar carpeta: quita sus canciones",
               library.getNumTracks(), 0);
        library.addFolder (music);
        const auto again = library.findByFile (maria);
        check (library.getNumTracks() == 3 && again && again->analysed, "volver a anadirla: vuelve con su analisis", library.getNumTracks(), 3);

        // Duracion (quedan dos de 20 s y una de 25 s): lo que no entra no se ve ni cuenta, y vuelve.
        library.setDurationLimits ({ 22.0, 0.0, 0.0, 0.0 });
        check (library.getNumTracks() == 1 && library.search ("").size() == 1 && library.getTrackPaths().size() == 1,
               "duracion: minimo 22 s deja solo la de 25 s", library.getNumTracks(), 1);
        library.setDurationLimits ({ 0.0, 22.0, 0.0, 0.0 });
        check (library.getNumTracks() == 2 && library.search ("lenta").empty(), "duracion: maximo 22 s quita la de 25 s", library.getNumTracks(), 2);
        check (! library.acceptsFile (slow) && library.acceptsFile (maria), "duracion: mirar un archivo", 0.0, 0.0);
        {
            Library reopened (formats);   // (otra conexion a la misma base: el ajuste esta guardado)
            reopened.open (dbFile);
            check (reopened.getDurationLimits().maxAudio == 22.0, "duracion: se guarda", reopened.getDurationLimits().maxAudio, 22.0);
        }
        library.setDurationLimits ({ 30.0, 0.0, 30.0, 0.0 });
        Library::SyncOptions shortOnly;
        shortOnly.roots.add (root);
        shortOnly.accept = [&library] (const juce::File& f) { return library.acceptsFile (f); };
        check (Library::findMediaFolders (shortOnly, nullptr, [] { return false; }, files).isEmpty(),
               "sincronizar: carpetas solo con audios cortos no entran", (double) files, 0.0);
        library.setDurationLimits ({});
        check (library.getNumTracks() == 3, "duracion: sin limite vuelven todas", library.getNumTracks(), 3);

        // Quitar el disco (por error) y sincronizar: la carpeta vuelve y lo nuevo tambien entra (la
        // exclusion del disco ya no la tapa).
        removal = library.removeFromLibrary (root);
        library.addFolder (music);
        const auto fresh = music.getChildFile ("Nueva - Cancion.wav");
        writeClickWav (fresh, 115.0);
        library.scanNow();
        check (library.getNumTracks() == 4 && library.findByFile (fresh).has_value(), "sincronizar tras quitar el disco: entra lo nuevo",
               library.getNumTracks(), 4);
        // Una subcarpeta quitada que ya esta dentro de otra: includeFolder la trae (y lo nuevo de dentro).
        library.removeFromLibrary (sub);
        const auto freshSub = sub.getChildFile ("Otra - Nueva.wav");
        writeClickWav (freshSub, 125.0);
        library.scanNow();
        check (library.getNumTracks() == 3, "subcarpeta quitada: lo nuevo de dentro no entra", library.getNumTracks(), 3);
        library.includeFolder (sub);
        library.scanNow();
        check (library.getNumTracks() == 5 && library.getFolders().size() == 1, "includeFolder: vuelve sin ser carpeta propia",
               library.getNumTracks(), 5);
        fresh.deleteFile();
        freshSub.deleteFile();
        library.scanNow();
        library.removeFolder (music);
    }
    {
        // El hilo de fondo escanea y analiza solo.
        const auto dbFile2 = root.getChildFile ("library2.db");
        Library library (formats);
        library.open (dbFile2);
        library.setDurationLimits ({});
        library.addFolder (music);
        library.startBackgroundWork();
        const auto deadline = juce::Time::getMillisecondCounter() + 60000;
        auto allDone = [&]
        {
            const auto all = library.search ("");
            return all.size() == 3 && std::all_of (all.begin(), all.end(), [] (const midj::TrackInfo& x) { return x.analysed; });
        };
        while (! allDone() && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep (50);
        check (allDone() && ! library.isBusy(), "hilo de fondo: escanea y analiza solo", (double) library.search ("").size(), 3.0);
        check (library.getStatusText() == "3 canciones", "estado al terminar", 0.0, 0.0);
    }
    root.deleteRecursively();
}


// --- Efectos -----------------------------------------------------------------------------

// Pasa 'buf' (estereo) por un efecto en bloques, con el tempo avanzando como en el deck.
void runEffect (midj::Effect& fx, juce::AudioBuffer<float>& buf, double bpm, int from = 0, int to = -1)
{
    if (to < 0)
        to = buf.getNumSamples();
    midj::TempoInfo t;
    t.samplesPerBeat = sr * 60.0 / bpm;
    t.synced = true;
    for (int start = from; start < to; start += blockSize)
    {
        const int n = std::min (blockSize, to - start);
        t.beatAtStart = start / t.samplesPerBeat;
        fx.process (buf.getWritePointer (0, start), buf.getWritePointer (1, start), n, t);
    }
}

juce::AudioBuffer<float> stereoNoise (double seconds, float level = 0.3f)
{
    juce::Random rng (11);
    juce::AudioBuffer<float> b (2, (int) (seconds * sr));
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * level);
    return b;
}

juce::AudioBuffer<float> stereoSine (double seconds, double hz, float level = 0.5f)
{
    juce::AudioBuffer<float> b (2, (int) (seconds * sr));
    for (int i = 0; i < b.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            b.setSample (ch, i, level * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr));
    return b;
}

bool sameRange (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from, int to)
{
    for (int ch = 0; ch < 2; ++ch)
        for (int i = from; i < to; ++i)
            if (a.getSample (ch, i) != b.getSample (ch, i))
                return false;
    return true;
}

float maxStep (const juce::AudioBuffer<float>& b, int from, int to)
{
    float m = 0.0f;
    for (int i = std::max (1, from); i < to; ++i)
        m = std::max (m, std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));
    return m;
}

std::vector<std::unique_ptr<midj::Effect>> allEffects()
{
    std::vector<std::unique_ptr<midj::Effect>> v;
    v.push_back (std::make_unique<midj::EchoEffect>());
    v.push_back (std::make_unique<midj::ReverbEffect>());
    v.push_back (std::make_unique<midj::FlangerEffect>());
    v.push_back (std::make_unique<midj::BitCrushEffect>());
    v.push_back (std::make_unique<midj::GateEffect>());
    for (auto& fx : v)
        fx->prepare (sr, blockSize);
    return v;
}

void testEffectsCommon()
{
    for (auto& fx : allEffects())
    {
        const juce::String name (fx->getName());
        const auto input = stereoNoise (12.0);

        // Apagado: la senal pasa intacta (ni un bit distinto).
        auto off = input;
        runEffect (*fx, off, 120.0);
        check (sameRange (off, input, 0, input.getNumSamples()) && fx->isIdle(),
               (name + ": apagado no toca la senal").toRawUTF8(), 0.0, 0.0);

        // Encendido 2 s y apagado: cambia la senal y despues vuelve a ser identica.
        fx->setAmount (0.8f);
        fx->setOn (true);
        auto out = input;
        runEffect (*fx, out, 120.0, 0, (int) (2.0 * sr));
        juce::AudioBuffer<float> diff (1, (int) sr);
        diff.copyFrom (0, 0, out, 0, (int) sr, (int) sr);
        diff.addFrom (0, 0, input, 0, (int) sr, (int) sr, -1.0f);
        const float change = diff.getRMSLevel (0, 0, (int) sr);
        check (change > 0.02f, (name + ": encendido cambia la senal (RMS de la diferencia)").toRawUTF8(), change, 0.02);

        fx->setOn (false);
        runEffect (*fx, out, 120.0, (int) (2.0 * sr), input.getNumSamples());
        // Tras 10 s apagado (colas incluidas) vuelve a pasar todo intacto.
        check (fx->isIdle() && sameRange (out, input, (int) (11.0 * sr), input.getNumSamples()),
               (name + ": al apagarlo vuelve a quedar intacto").toRawUTF8(), 0.0, 0.0);
        fx->reset();
    }

    // Sin clics: encender y apagar con un seno de 220 Hz; el salto maximo entre muestras no
    // crece (un seno de 0,5 a 220 Hz salta como mucho 0,016 por muestra).
    for (auto& fx : allEffects())
    {
        const juce::String name (fx->getName());
        if (name == "BitCrush" || name == "Gate")
            continue;   // su sonido son escalones y cortes: el salto es el efecto, no un clic
        auto sine = stereoSine (3.0, 220.0);
        fx->setAmount (0.7f);
        runEffect (*fx, sine, 120.0, 0, (int) sr);
        fx->setOn (true);
        runEffect (*fx, sine, 120.0, (int) sr, (int) (2.0 * sr));
        fx->setOn (false);
        runEffect (*fx, sine, 120.0, (int) (2.0 * sr), sine.getNumSamples());
        const float step = maxStep (sine, 1, sine.getNumSamples());
        check (step < 0.06f, (name + ": encender y apagar sin clics (salto maximo)").toRawUTF8(), step, 0.016);
    }
}

void testEcho()
{
    // Un golpe a 120 BPM: eco cada medio pulso (11025 muestras), cada vez mas bajo.
    midj::EchoEffect echo;
    echo.prepare (sr, blockSize);
    echo.setAmount (0.6f);
    echo.setOn (true);
    juce::AudioBuffer<float> b (2, (int) (3.0 * sr));
    b.clear();
    const int hit = (int) (0.1 * sr);
    for (int i = 0; i < 64; ++i)
        for (int ch = 0; ch < 2; ++ch)
            b.setSample (ch, hit + i, 0.8f * (float) std::sin (i * 0.3));
    runEffect (echo, b, 120.0);

    // Pico de cada golpe y su comienzo (primera muestra por encima de 0,01). Los filtros de la
    // realimentacion cambian la forma del golpe, asi que la posicion se mide por el comienzo.
    auto peakNear = [&b] (int centre, int& onset)
    {
        float best = 0.0f;
        onset = -1;
        for (int i = centre - 200; i < centre + 400; ++i)
        {
            const float v = std::abs (b.getSample (0, i));
            if (onset < 0 && v > 0.01f)
                onset = i;
            best = std::max (best, v);
        }
        return best;
    };
    int at1 = 0, at2 = 0, at0 = 0;
    const float p0 = peakNear (hit, at0), p1 = peakNear (hit + 11025, at1), p2 = peakNear (hit + 22050, at2);
    check (std::abs ((at1 - at0) - 11025) <= 3, "echo: repite a medio pulso (11025 muestras a 120 BPM)", at1 - at0, 11025);
    check (std::abs ((at2 - at1) - 11025) <= 3, "echo: segunda repeticion otro medio pulso despues", at2 - at1, 11025);
    check (p1 > 0.1f && p2 < p1 && p1 < p0, "echo: cada repeticion mas baja", p2, p1);

    // La cola sigue al apagar: el golpe, el eco se apaga justo despues del 1er eco, y el 2o suena.
    echo.reset();
    echo.setOn (true);
    b.clear();
    for (int i = 0; i < 64; ++i)
        for (int ch = 0; ch < 2; ++ch)
            b.setSample (ch, hit + i, 0.8f * (float) std::sin (i * 0.3));
    runEffect (echo, b, 120.0, 0, hit + 11025 + 2000);
    echo.setOn (false);
    runEffect (echo, b, 120.0, hit + 11025 + 2000, b.getNumSamples());
    int at = 0;
    check (peakNear (hit + 22050, at) > 0.05f, "echo: al apagarlo la cola sigue sonando", peakNear (hit + 22050, at), 0.05);

    // Cambio de tempo con el eco sonando: fundido de 64 muestras, sin clic.
    echo.reset();
    echo.setOn (true);
    auto sine = stereoSine (4.0, 220.0);
    runEffect (echo, sine, 120.0, 0, (int) (2.0 * sr));
    runEffect (echo, sine, 126.0, (int) (2.0 * sr), sine.getNumSamples());
    const float step = maxStep (sine, (int) (1.5 * sr), (int) (2.5 * sr));
    check (step < 0.06f, "echo: cambiar el BPM sonando no hace clic", step, 0.03);
}

void testBitCrushAndGate()
{
    // BitCrush a tope: 4 bits (multiplos de 1/8) y cada valor se mantiene 1024 muestras.
    check (midj::BitCrushEffect::bitsFor (0.0f) == 16.0f && midj::BitCrushEffect::bitsFor (1.0f) == 4.0f,
           "bitcrush: de 16 a 4 bits (formula de VirtualDJ)", midj::BitCrushEffect::bitsFor (1.0f), 4.0);
    midj::BitCrushEffect crush;
    crush.setAmount (1.0f);
    crush.setOn (true);
    crush.prepare (sr, blockSize);
    auto ramp = stereoSine (2.0, 3.0, 0.9f);
    runEffect (crush, ramp, 120.0);
    bool onGrid = true;
    int longestRun = 0, run = 1;
    for (int i = (int) sr; i < ramp.getNumSamples(); ++i)
    {
        const float v = ramp.getSample (0, i) * 8.0f;
        onGrid = onGrid && std::abs (v - std::round (v)) < 1e-4f;
        run = ramp.getSample (0, i) == ramp.getSample (0, i - 1) ? run + 1 : 1;
        longestRun = std::max (longestRun, run);
    }
    check (onGrid, "bitcrush a tope: valores en multiplos de 1/8 (4 bits)", 0.0, 0.0);
    check (longestRun >= 1024, "bitcrush a tope: retiene 1024 muestras", longestRun, 1024);

    // Gate a 120 BPM: abierto en la primera mitad de cada cuarto de pulso, cerrado en la segunda.
    midj::GateEffect gate;
    gate.prepare (sr, blockSize);
    gate.setAmount (1.0f);
    gate.setOn (true);
    juce::AudioBuffer<float> dc (2, (int) (4.0 * sr));
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.5f, dc.getNumSamples());
    runEffect (gate, dc, 120.0);
    const double quarter = 22050.0 / 4.0;
    bool aligned = true;
    for (int k = 8; k < 40; ++k)
    {
        const float openValue = dc.getSample (0, (int) (k * quarter + quarter * 0.25));
        const float closedValue = dc.getSample (0, (int) (k * quarter + quarter * 0.75));
        aligned = aligned && openValue > 0.49f && closedValue < 0.01f;
    }
    check (aligned, "gate: abre y cierra cada cuarto de pulso, en su sitio", 0.0, 0.0);
}

void testEffectRack()
{
    midj::EffectRack rack;
    rack.prepare (sr, blockSize);
    check (rack.getEffect (0) == 0 && rack.getEffect (1) == 2 && rack.getEffect (2) == 4,
           "rack: ranuras con Echo, Flanger y Gate", 0.0, 0.0);
    rack.setAmount (0, 0.7f);
    rack.setOn (0, true);
    rack.setEffect (0, 3);   // cambiar a BitCrush sonando: sigue encendido y con el mismo mando
    check (rack.getEffect (0) == 3 && rack.isOn (0) && rack.getAmount (0) == 0.7f,
           "rack: cambiar de efecto conserva ON y el mando", 0.0, 0.0);

    // Los efectos de un deck no tocan al otro: BitCrush en A no cambia B (crossfader a B).
    juce::AudioFormatManager formats;
    midj::Deck a (formats), b (formats);
    midj::Mixer mixer (a, b);
    mixer.prepareToPlay (blockSize, sr);
    juce::AudioBuffer<float> tone (1, (int) (sr * 5.0));
    for (int i = 0; i < tone.getNumSamples(); ++i)
        tone.setSample (0, i, 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * i / sr));
    a.loadBuffer (tone, sr, "a", { 120.0, 0.0 });
    b.loadBuffer (tone, sr, "b", { 120.0, 0.0 });
    a.setKeyLock (false);
    b.setKeyLock (false);
    a.play();
    b.play();
    mixer.setCrossfader (1.0f);
    auto render = [&] (int blocks)
    {
        juce::AudioBuffer<float> out (2, blocks * blockSize);
        for (int k = 0; k < blocks; ++k)
        {
            juce::AudioSourceChannelInfo info (&out, k * blockSize, blockSize);
            mixer.getNextAudioBlock (info);
        }
        return out;
    };
    const auto before = render (40);
    a.setPositionSeconds (0.0);
    b.setPositionSeconds (0.0);
    mixer.getEffects (0).setEffect (0, 3);
    mixer.getEffects (0).setAmount (0, 1.0f);
    mixer.getEffects (0).setOn (0, true);
    render (1);   // el salto se aplica en este bloque
    a.setPositionSeconds (0.0);
    b.setPositionSeconds (0.0);
    const auto after = render (40);
    // Los filtros del canal arrastran memoria de lo anterior: se compara pasado ese arranque.
    float worst = 0.0f;
    for (int i = blockSize * 10; i < before.getNumSamples(); ++i)
        worst = std::max (worst, std::abs (before.getSample (0, i) - after.getSample (0, i)));
    check (worst < 1e-4f, "mezclador: los efectos de A no tocan B (diferencia maxima)", worst, 0.0);
}

void benchmarkEffects()
{
    // Los 5 efectos encendidos a la vez en una ranura cada uno no es posible (3 ranuras):
    // se mide cada efecto encendido por separado.
    for (auto& fx : allEffects())
    {
        fx->setOn (true);
        fx->setAmount (0.8f);
        auto noise = stereoNoise (10.0);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        runEffect (*fx, noise, 120.0);
        const double load = (juce::Time::getMillisecondCounterHiRes() - start) / 10000.0;
        std::printf ("[INFO]  %-9s %.2f %% de un nucleo\n", fx->getName(), load * 100.0);
    }
}


// --- Controladoras MIDI ------------------------------------------------------------------

void testMidiValues()
{
    using B = midj::MidiBinding;
    check (B::unitFromCC (0) == 0.0 && B::unitFromCC (64) == 0.5 && B::unitFromCC (127) == 1.0,
           "MIDI: CC 0/64/127 -> 0 / 0,5 exacto / 1", B::unitFromCC (64), 0.5);
    check (B::unitFromPitchBend (0) == 0.0 && B::unitFromPitchBend (8192) == 0.5 && B::unitFromPitchBend (16383) == 1.0,
           "MIDI: pitch bend 0/8192/16383 -> 0 / 0,5 / 1", B::unitFromPitchBend (8192), 0.5);
    check (B::ticksFrom (1, B::Encoding::relativeTwos) == 1 && B::ticksFrom (127, B::Encoding::relativeTwos) == -1
               && B::ticksFrom (3, B::Encoding::relativeTwos) == 3,
           "MIDI: encoder en complemento a dos (1 = +1, 127 = -1)", 0.0, 0.0);
    check (B::ticksFrom (65, B::Encoding::relativeOffset) == 1 && B::ticksFrom (63, B::Encoding::relativeOffset) == -1,
           "MIDI: encoder desplazado (65 = +1, 63 = -1)", 0.0, 0.0);

    // Mapeo: un control hace una sola accion; se guarda y se lee igual.
    midj::MidiMapping mapping;
    midj::MidiBinding play;
    play.kind = B::Kind::note;
    play.number = 36;
    play.action = "deck1.play";
    mapping.bind (play);
    auto cue = play;
    cue.action = "deck1.cue";
    mapping.bind (cue);   // misma nota: sustituye
    check (mapping.getAll().size() == 1 && mapping.getAll()[0].action == "deck1.cue", "mapeo: un control, una accion", 0.0, 0.0);
    midj::MidiBinding jog;
    jog.channel = 2;
    jog.number = 20;
    jog.encoding = B::Encoding::relativeOffset;
    jog.action = "deck2.jog";
    mapping.bind (jog);
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsMidi.xml");
    mapping.save (file);
    midj::MidiMapping loaded;
    loaded.load (file);
    const auto* back = loaded.find (jog);
    check (loaded.getAll().size() == 2 && back != nullptr && back->action == "deck2.jog"
               && back->encoding == B::Encoding::relativeOffset && back->channel == 2,
           "mapeo: guardar y leer el XML", (double) loaded.getAll().size(), 2.0);
    file.deleteFile();
}

void testMidiController()
{
    juce::AudioFormatManager formats;
    midj::Deck a (formats), b (formats);
    midj::Mixer mixer (a, b);
    mixer.prepareToPlay (blockSize, sr);
    a.loadBuffer (toBuffer (clickTrack (120.0, 30.0, sr, 0.25)), sr, "a", { 120.0, 0.25 });
    b.loadBuffer (toBuffer (clickTrack (128.0, 30.0, sr, 0.25)), sr, "b", { 128.0, 0.25 });
    midj::ControlSurface surface (a, b, mixer);
    midj::MidiController midi (surface);
    midi.setMappingFile (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsMidi2.xml"));

    using M = juce::MidiMessage;
    // Aprender: Play A con la nota 36 del canal 1.
    midi.startLearning ("deck1.play");
    midi.handleMessage (M::noteOn (1, 36, (juce::uint8) 127));
    check (midi.getLearningAction().isEmpty() && midi.describeBindings ("deck1.play") == "Nota 36 (canal 1)",
           "aprender: Play A = nota 36", 0.0, 0.0);
    check (! a.isPlaying(), "aprender: el mensaje que se aprende no ejecuta la accion", 0.0, 0.0);
    midi.handleMessage (M::noteOn (1, 36, (juce::uint8) 127));
    check (a.isPlaying(), "nota 36: Play A", 1.0, 1.0);
    midi.handleMessage (M::noteOff (1, 36));
    check (a.isPlaying(), "soltar la nota no cambia nada", 1.0, 1.0);
    midi.handleMessage (M::noteOn (1, 36, (juce::uint8) 100));
    check (! a.isPlaying(), "nota 36 otra vez: pausa", 0.0, 0.0);

    // Volumen B con el CC 7 del canal 2.
    midi.startLearning ("deck2.volume");
    midi.handleMessage (M::controllerEvent (2, 7, 10));
    midi.handleMessage (M::controllerEvent (2, 7, 64));
    check (mixer.getStrip (1).getVolume() == 0.5f, "CC 7 = 64: volumen B al 50 %", mixer.getStrip (1).getVolume(), 0.5);
    midi.handleMessage (M::controllerEvent (2, 7, 127));
    check (mixer.getStrip (1).getVolume() == 1.0f, "CC 7 = 127: volumen B al maximo", mixer.getStrip (1).getVolume(), 1.0);
    midi.handleMessage (M::controllerEvent (1, 7, 0));
    check (mixer.getStrip (1).getVolume() == 1.0f, "el mismo CC en otro canal no hace nada", mixer.getStrip (1).getVolume(), 1.0);

    // Un fader no aprende de una nota; si de un pitch bend (faders de alta resolucion).
    midi.startLearning ("crossfader");
    midi.handleMessage (M::noteOn (1, 50, (juce::uint8) 127));
    check (midi.getLearningAction() == "crossfader", "aprender un fader ignora las notas", 0.0, 0.0);
    midi.handleMessage (M::pitchWheel (3, 8192));
    midi.handleMessage (M::pitchWheel (3, 16383));
    check (mixer.getCrossfader() == 1.0f, "pitch bend: crossfader a la derecha", mixer.getCrossfader(), 1.0);
    midi.handleMessage (M::pitchWheel (3, 8192));
    check (mixer.getCrossfader() == 0.5f, "pitch bend centro: crossfader al centro", mixer.getCrossfader(), 0.5);

    // Tempo A: +-16 %.
    midi.startLearning ("deck1.tempo");
    midi.handleMessage (M::controllerEvent (1, 1, 64));
    check (std::abs (a.getSpeed() - 1.0) < 1e-9, "tempo CC 64: velocidad normal", a.getSpeed(), 1.0);
    midi.handleMessage (M::controllerEvent (1, 1, 127));
    check (std::abs (a.getSpeed() - 1.16) < 1e-9, "tempo CC 127: +16 %", a.getSpeed(), 1.16);
    midi.handleMessage (M::controllerEvent (1, 1, 0));
    check (std::abs (a.getSpeed() - 0.84) < 1e-9, "tempo CC 0: -16 %", a.getSpeed(), 0.84);
    a.setSpeed (1.0);

    // Jog A, encoder en complemento a dos: parado, cada paso mueve 10 ms.
    midi.startLearning ("deck1.jog");
    midi.handleMessage (M::controllerEvent (1, 20, 1));
    check (midi.getMappingCopy().bindingsFor ("deck1.jog")[0].encoding == midj::MidiBinding::Encoding::relativeTwos,
           "aprender jog: detecta complemento a dos", 0.0, 0.0);
    a.setPositionSeconds (5.0);
    for (int i = 0; i < 10; ++i)
        midi.handleMessage (M::controllerEvent (1, 20, 1));
    check (std::abs (a.getPositionSeconds() - 5.1) < 1e-6, "jog parado: 10 pasos = +0,1 s", a.getPositionSeconds(), 5.1);
    midi.handleMessage (M::controllerEvent (1, 20, 127));
    check (std::abs (a.getPositionSeconds() - 5.09) < 1e-6, "jog parado: paso atras", a.getPositionSeconds(), 5.09);

    // Sonando, el jog empuja el tempo un momento y el empuje se desvanece.
    a.play();
    render (a, 0.1);
    const double before = a.getPositionSeconds();
    for (int i = 0; i < 10; ++i)
        midi.handleMessage (M::controllerEvent (1, 20, 1));
    check (std::abs (a.getJogBend() - 0.04) < 1e-9, "jog sonando: empuje de +4 %", a.getJogBend(), 0.04);
    render (a, 1.0);
    // render() pide bloques enteros: lo que de verdad sono es un poco mas de 1 s.
    const double rendered = std::ceil (sr / blockSize) * blockSize / sr;
    const double extra = a.getPositionSeconds() - before - rendered;
    check (a.getJogBend() < 1e-4 && extra > 0.003 && extra < 0.005, "jog sonando: adelanta 4 ms (0,04 x 0,1 s) y se desvanece",
           extra * 1000.0, 4.0);
    a.pause();

    // Navegador con encoder desplazado (65 = +1).
    midi.startLearning ("browser.scroll");
    midi.handleMessage (M::controllerEvent (1, 21, 65));
    check (midi.getMappingCopy().bindingsFor ("browser.scroll")[0].encoding == midj::MidiBinding::Encoding::relativeOffset,
           "aprender encoder: detecta el desplazado (65/63)", 0.0, 0.0);

    // Botones por CC: 127 al pulsar, 0 al soltar (el 0 no hace nada).
    midi.startLearning ("deck2.loop4");
    midi.handleMessage (M::controllerEvent (1, 50, 0));     // un 0 no sirve para aprender un boton
    midi.handleMessage (M::controllerEvent (1, 50, 127));
    b.setPositionSeconds (3.0);
    midi.handleMessage (M::controllerEvent (1, 50, 127));
    midi.handleMessage (M::controllerEvent (1, 50, 0));
    check (b.getLoopBeats() == 4, "boton por CC: loop de 4 en B (soltar no lo quita)", b.getLoopBeats(), 4);
    midi.handleMessage (M::controllerEvent (1, 50, 127));
    check (b.getLoopBeats() == 0, "boton por CC otra vez: quita el loop", b.getLoopBeats(), 0);

    // Efectos: encender FX2 de A y su mando.
    midi.startLearning ("deck1.fx2");
    midi.handleMessage (M::noteOn (1, 40, (juce::uint8) 127));
    midi.handleMessage (M::noteOn (1, 40, (juce::uint8) 127));
    check (mixer.getEffects (0).isOn (1), "nota 40: enciende FX2 de A", 1.0, 1.0);
    midi.startLearning ("deck1.fx2_amount");
    midi.handleMessage (M::controllerEvent (1, 30, 5));
    midi.handleMessage (M::controllerEvent (1, 30, 127));
    check (mixer.getEffects (0).getAmount (1) == 1.0f, "CC 30: mando de FX2 de A al maximo", mixer.getEffects (0).getAmount (1), 1.0);

    // EQ: graves de A al minimo (kill) y de vuelta al centro.
    midi.startLearning ("deck1.low");
    midi.handleMessage (M::controllerEvent (1, 31, 0));
    midi.handleMessage (M::controllerEvent (1, 31, 0));
    check (mixer.getStrip (0).getLow() == 0.0f, "CC 31 = 0: graves de A a cero", mixer.getStrip (0).getLow(), 0.0);
    midi.handleMessage (M::controllerEvent (1, 31, 64));
    check (mixer.getStrip (0).getLow() == 0.5f, "CC 31 = 64: graves de A al centro", mixer.getStrip (0).getLow(), 0.5);

    // Quitar una asignacion; un mensaje sin asignar no hace nada pero se muestra.
    midi.removeAction ("deck1.play");
    midi.handleMessage (M::noteOn (1, 36, (juce::uint8) 127));
    check (! a.isPlaying() && midi.getLastMessageText() == "Nota 36 (canal 1) = 127", "quitar asignacion: la nota ya no hace nada",
           0.0, 0.0);

    // Se lee un mapeo guardado y funciona igual.
    const auto saved = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsMidi3.xml");
    midi.getMappingCopy().save (saved);
    midj::MidiController other (surface);
    other.loadMapping (saved);
    other.handleMessage (M::controllerEvent (2, 7, 64));
    check (mixer.getStrip (1).getVolume() == 0.5f, "mapeo leido de disco: CC 7 sigue moviendo el volumen B",
           mixer.getStrip (1).getVolume(), 0.5);
    saved.deleteFile();

    // Muchos mensajes desde otro hilo mientras suena el audio: sin bloqueos ni fallos.
    a.play();
    juce::WaitableEvent finished;
    juce::Thread::launch ([&midi, &finished]
    {
        for (int i = 0; i < 20000; ++i)
            midi.handleMessage (M::controllerEvent (1, 31, i % 128));
        finished.signal();
    });
    render (a, 2.0);
    finished.wait (10000);
    a.pause();
    check (true, "MIDI desde otro hilo con el audio sonando", 0.0, 0.0);
}


// --- Sampler -----------------------------------------------------------------------------

std::shared_ptr<midj::SampleData> toneSample (double seconds, double hz, double bpm = 0.0)
{
    auto s = std::make_shared<midj::SampleData>();
    s->audio = stereoSine (seconds, hz, 0.5f);
    s->sampleRate = sr;
    s->bpm = bpm;
    s->name = "tono";
    return s;
}

juce::AudioBuffer<float> runSampler (midj::Sampler& sampler, double seconds, midj::Sampler::Reference ref = {})
{
    const int blocks = (int) (seconds * sr) / blockSize;
    juce::AudioBuffer<float> out (2, blocks * blockSize);
    out.clear();
    for (int k = 0; k < blocks; ++k)
    {
        sampler.process (out.getWritePointer (0, k * blockSize), out.getWritePointer (1, k * blockSize), blockSize, ref);
        if (ref.valid)
            ref.beatAtStart += blockSize / ref.samplesPerBeat;
    }
    return out;
}

void testSampleData()
{
    using midj::Sampler;
    check (std::abs (Sampler::syncSpeed (120.0, 130.2) - 120.0 / 130.2) < 1e-9, "sync: 120 / 130,2 = 0,9217 (Heart Beat en VirtualDJ)",
           Sampler::syncSpeed (120.0, 130.2), 0.9217);
    double factor = 0.0;
    check (std::abs (Sampler::syncSpeed (128.0, 60.0, &factor) - 128.0 / 120.0) < 1e-9 && factor == 0.5,
           "sync: 128 / 60 se pliega a 1,067 (medio pulso)", Sampler::syncSpeed (128.0, 60.0), 1.0667);
    check (std::abs (Sampler::syncSpeed (60.0, 128.0, &factor) - 0.9375) < 1e-9 && factor == 2.0,
           "sync: 60 / 128 se pliega a 0,9375 (doble)", Sampler::syncSpeed (60.0, 128.0), 0.9375);

    // .vdjsample sintetico: cabecera de VirtualDJ + 2 s de Ogg; fragmento de 0,5 a 1,5 s.
    juce::MemoryBlock audio;
    {
        juce::OggVorbisAudioFormat format;
        auto tone = stereoSine (2.0, 440.0, 0.5f);
        std::unique_ptr<juce::AudioFormatWriter> w (format.createWriterFor (new juce::MemoryOutputStream (audio, false), sr, 2, 16, {}, 5));
        w->writeFromAudioSampleBuffer (tone, 0, tone.getNumSamples());
    }   // al destruir el escritor el Ogg queda completo en 'audio'
    juce::MemoryBlock file (0x80 + audio.getSize(), true);
    auto put = [&file] (size_t at, const void* v, size_t n) { std::memcpy (static_cast<char*> (file.getData()) + at, v, n); };
    const uint32_t header = 0x80, size = (uint32_t) audio.getSize(), version = 852, loopMode = 3;
    const uint8_t isLoop = 1;
    const float secondsPerBeat = 0.5f, phase = 0.1f, gain = 0.8f;
    const double from = 0.5, length = 1.0;
    put (0, "VDJ", 4); put (4, &version, 4); put (8, &header, 4); put (12, &size, 4);
    put (0x18, &isLoop, 1); put (0x1C, &loopMode, 4); put (0x20, &secondsPerBeat, 4); put (0x24, &phase, 4);
    put (0x28, &from, 8); put (0x30, &length, 8); put (0x48, &gain, 4);
    put (0x80, audio.getData(), audio.getSize());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const auto s = midj::SampleData::loadVdjSample (file, "prueba", formats);
    check (s != nullptr, ".vdjsample: se lee", 1.0, 1.0);
    if (s == nullptr)
        return;
    check (std::abs (s->lengthSeconds() - 1.0) < 0.001, ".vdjsample: recorta el fragmento (1 s)", s->lengthSeconds(), 1.0);
    check (std::abs (s->bpm - 120.0) < 1e-3 && s->lengthBeats() == 2, ".vdjsample: 120 BPM, 2 pulsos", s->bpm, 120.0);
    check (std::abs (s->firstBeat - 0.1) < 1e-6, ".vdjsample: fase llevada al fragmento", s->firstBeat, 0.1);
    check (s->loop && s->tempoMode == 2 && std::abs (s->gain - 0.8f) < 1e-6f, ".vdjsample: bucle, locked y ganancia", s->gain, 0.8);
    check (std::abs (sineFrequency (s->audio, 1000, s->audio.getNumSamples() - 1000) - 440.0) < 1.0,
           ".vdjsample: el audio Ogg suena a 440 Hz", sineFrequency (s->audio, 1000, s->audio.getNumSamples() - 1000), 440.0);
    file.setSize (40);
    check (midj::SampleData::loadVdjSample (file, "rota", formats) == nullptr, ".vdjsample corto o roto: no se lee", 0.0, 0.0);
}

void testSampler()
{
    using midj::Sampler;
    Sampler sampler;
    sampler.prepare (sr, blockSize);
    sampler.setVolume (1.0f);
    sampler.setSample (0, toneSample (1.0, 440.0));

    // On/off: arranca con fundido (sin clic) y se para con otro fundido.
    sampler.press (0);
    auto out = runSampler (sampler, 0.3);
    check (sampler.isPlaying (0) && out.getRMSLevel (0, 0, out.getNumSamples()) > 0.3f, "pad on/off: suena", out.getRMSLevel (0, 0, out.getNumSamples()), 0.35);
    check (std::abs (out.getSample (0, 1)) < 0.02f && maxStep (out, 1, out.getNumSamples()) < 0.04f,
           "pad: arranca con fundido de 128 muestras", maxStep (out, 1, out.getNumSamples()), 0.03);
    sampler.press (0);
    out = runSampler (sampler, 0.1);
    check (! sampler.isPlaying (0) && out.getMagnitude (0, 200, out.getNumSamples() - 200) == 0.0f
               && maxStep (out, 1, 200) < 0.04f,
           "pad on/off: segunda pulsacion lo para (con fundido)", out.getMagnitude (0, 200, out.getNumSamples() - 200), 0.0);

    // Sin bucle se para solo al acabar; con bucle sigue.
    sampler.press (0);
    out = runSampler (sampler, 1.3);
    check (! sampler.isPlaying (0) && out.getMagnitude (0, (int) (1.05 * sr), out.getNumSamples() - (int) (1.05 * sr)) == 0.0f,
           "pad sin bucle: se para solo al acabar", 0.0, 0.0);
    sampler.setLoop (0, true);
    sampler.press (0);
    out = runSampler (sampler, 2.5);
    check (sampler.isPlaying (0) && out.getRMSLevel (0, (int) (2.0 * sr), (int) (0.4 * sr)) > 0.3f, "pad con bucle: sigue sonando", 1.0, 1.0);
    sampler.stopAll();
    runSampler (sampler, 0.05);
    check (! sampler.isPlaying (0), "STOP: para todos los pads", 0.0, 0.0);

    // Hold: suena mientras se mantiene.
    sampler.setTrigger (0, Sampler::Trigger::hold);
    sampler.press (0);
    runSampler (sampler, 0.2);
    const bool heldPlaying = sampler.isPlaying (0);
    sampler.release (0);
    runSampler (sampler, 0.05);
    check (heldPlaying && ! sampler.isPlaying (0), "pad hold: suena al pulsar y calla al soltar", 0.0, 0.0);

    // Stutter: cada pulsacion vuelve a empezar (con fundido, sin clic).
    sampler.setTrigger (0, Sampler::Trigger::stutter);
    sampler.setLoop (0, false);
    sampler.press (0);
    runSampler (sampler, 0.5);
    const double before = sampler.getProgress (0);
    sampler.press (0);
    out = runSampler (sampler, 0.1);
    check (before > 0.45 && sampler.getProgress (0) < 0.15 && maxStep (out, 1, out.getNumSamples()) < 0.04f,
           "pad stutter: vuelve al principio sin clic", sampler.getProgress (0), 0.1);
    sampler.stopAll();
    runSampler (sampler, 0.05);

    // Sincronizado: un sonido de 130,2 BPM con el deck a 120 suena a 0,9217 de velocidad.
    sampler.setTrigger (1, Sampler::Trigger::onOff);
    sampler.setSample (1, toneSample (3.0, 440.0, 130.2));
    sampler.setLoop (1, true);
    sampler.setTempoMode (1, Sampler::TempoMode::sync);
    Sampler::Reference ref;
    ref.valid = true;
    ref.bpm = 120.0;
    ref.samplesPerBeat = sr / 2.0;
    sampler.press (1);
    out = runSampler (sampler, 1.5, ref);
    const double freq = sineFrequency (out, (int) (0.2 * sr), out.getNumSamples());
    check (std::abs (freq - 440.0 * 120.0 / 130.2) < 0.5, "pad sincronizado: sigue el tempo del deck (405,5 Hz)", freq, 405.5);
    sampler.press (1);
    runSampler (sampler, 0.05);

    // Bloqueado: un bucle de 2 pulsos con un golpe en cada pulso; los golpes caen en los pulsos
    // del deck aunque se dispare a mitad de pulso.
    auto loopSample = std::make_shared<midj::SampleData>();
    const auto clicks = clickTrack (130.2, 2.0 * 60.0 / 130.2, sr, 0.0);
    loopSample->audio = toBuffer (clicks);
    loopSample->audio.setSize (2, (int) clicks.size(), true);
    loopSample->audio.copyFrom (1, 0, loopSample->audio, 0, 0, (int) clicks.size());
    loopSample->sampleRate = sr;
    loopSample->bpm = 130.2;
    loopSample->loop = true;
    loopSample->tempoMode = (int) Sampler::TempoMode::locked;
    sampler.setSample (2, loopSample);
    ref.beatAtStart = 0.37;   // el deck va por el 37 % de un pulso al disparar
    sampler.press (2);
    out = runSampler (sampler, 6.0, ref);
    double worst = 0.0;
    int hits = 0;
    for (int i = 1; i < out.getNumSamples(); ++i)
        if (std::abs (out.getSample (0, i)) > 0.2f && std::abs (out.getSample (0, i - 1)) <= 0.2f && i > 1000)
        {
            const double beat = 0.37 + i / ref.samplesPerBeat;   // pulso del deck en esa muestra
            worst = std::max (worst, std::abs (beat - std::round (beat)));
            ++hits;
            i += (int) (0.2 * sr);
        }
    check (hits >= 10 && worst < 0.02, "pad bloqueado: los golpes caen en los pulsos del deck", worst, 0.0);
    sampler.press (2);
    runSampler (sampler, 0.05);
}

void testSamplerRecording()
{
    // Grabar 4 pulsos del deck A (bombos a 120 BPM): empieza en el pulso y dura 2 s exactos.
    juce::AudioFormatManager formats;
    midj::Deck a (formats), b (formats);
    midj::Mixer mixer (a, b);
    mixer.prepareToPlay (blockSize, sr);
    a.loadBuffer (toBuffer (clickTrack (120.0, 30.0, sr, 0.25)), sr, "a", { 120.0, 0.25 });
    a.setKeyLock (false);
    a.setPositionSeconds (1.1);   // a mitad de pulso
    a.play();
    auto& sampler = mixer.getSampler();
    sampler.startRecording (3, 0, 4);
    check (sampler.getRecordingPad() == 3, "grabar: el pad 4 espera el pulso", sampler.getRecordingPad(), 3);
    juce::AudioBuffer<float> out (2, blockSize);
    for (int k = 0; k < (int) (3.0 * sr) / blockSize; ++k)
    {
        juce::AudioSourceChannelInfo info (&out, 0, blockSize);
        mixer.getNextAudioBlock (info);
    }
    sampler.finishPendingRecording();
    const auto rec = sampler.getSample (3);
    check (rec != nullptr && rec->audio.getNumSamples() == 88200, "grabar: 4 pulsos a 120 BPM = 88200 muestras",
           rec ? rec->audio.getNumSamples() : 0, 88200);
    if (rec == nullptr)
        return;
    check (std::abs (rec->bpm - 120.0) < 1e-6 && rec->loop && rec->tempoMode == 2, "grabar: queda como bucle bloqueado a 120 BPM",
           rec->bpm, 120.0);
    // Los golpes del deck caen en las muestras 0, 22050, 44100 y 66150 de la grabacion. Cada golpe
    // es un seno que empieza en 0: pasa de 0,05 unas 8 muestras despues de empezar.
    const int risesAfter = (int) std::ceil (std::asin (0.05 / 0.8) / (2.0 * juce::MathConstants<double>::pi * 60.0) * sr);
    bool onBeats = true;
    for (int beat = 0; beat < 4; ++beat)
    {
        int onset = -1;
        for (int i = beat * 22050 - 30; i < beat * 22050 + 400 && onset < 0; ++i)
            if (i >= 0 && std::abs (rec->audio.getSample (0, i)) > 0.05f)
                onset = i;
        onBeats = onBeats && onset >= 0 && std::abs (onset - (beat * 22050 + risesAfter)) <= 3;
    }
    check (onBeats, "grabar: empieza justo en el pulso (golpes cada 22050 muestras)", 0.0, 0.0);
    check (sampler.getRecordingPad() == -1, "grabar: termina sola", 0.0, 0.0);

    // El sampler va a la salida general: el crossfader no lo corta.
    a.pause();
    mixer.setCrossfader (1.0f);   // solo el deck B (que no tiene cancion)
    sampler.setVolume (1.0f);
    sampler.press (3);
    double energy = 0.0;
    for (int k = 0; k < 40; ++k)
    {
        juce::AudioSourceChannelInfo info (&out, 0, blockSize);
        mixer.getNextAudioBlock (info);
        energy += out.getRMSLevel (0, 0, blockSize);
    }
    check (energy > 0.5, "sampler: suena aunque el crossfader este en el otro deck", energy, 1.0);
    // Guardar y leer el banco: la grabacion se guarda como WAV y vuelve con su tempo y ajustes.
    sampler.stopAll();
    sampler.setTrigger (3, midj::Sampler::Trigger::stutter);
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsSampler");
    dir.deleteRecursively();
    check (sampler.saveBank (dir.getChildFile ("sampler.xml"), dir.getChildFile ("Grabaciones")),
           "banco: se guarda", 1.0, 1.0);
    check (dir.getChildFile ("Grabaciones").getNumberOfChildFiles (juce::File::findFiles) == 1, "banco: la grabacion queda como WAV", 1.0, 1.0);
    midj::Sampler reloaded;
    reloaded.prepare (sr, blockSize);
    juce::AudioFormatManager wavFormats;
    wavFormats.registerBasicFormats();
    reloaded.loadBank (dir.getChildFile ("sampler.xml"), wavFormats);
    const auto back = reloaded.getSample (3);
    check (back != nullptr && back->audio.getNumSamples() == 88200 && std::abs (back->bpm - 120.0) < 1e-6
               && reloaded.getLoop (3) && reloaded.getTempoMode (3) == midj::Sampler::TempoMode::locked
               && reloaded.getTrigger (3) == midj::Sampler::Trigger::stutter,
           "banco: al leerlo el pad vuelve igual (audio, BPM, bucle, modos)", back ? back->audio.getNumSamples() : 0, 88200);
    sampler.saveBank (dir.getChildFile ("sampler.xml"), dir.getChildFile ("Grabaciones"));
    check (dir.getChildFile ("Grabaciones").getNumberOfChildFiles (juce::File::findFiles) == 1, "banco: guardar otra vez no duplica el WAV", 1.0, 1.0);
    dir.deleteRecursively();

    // MIDI: un pad en modo hold suena mientras se mantiene la nota.
    midj::ControlSurface surface (a, b, mixer);
    midj::MidiController midi (surface);
    midi.setMappingFile (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsMidi4.xml"));
    midi.startLearning ("sampler.pad4");
    midi.handleMessage (juce::MidiMessage::noteOn (10, 60, (juce::uint8) 127));
    sampler.setTrigger (3, midj::Sampler::Trigger::hold);
    midi.handleMessage (juce::MidiMessage::noteOn (10, 60, (juce::uint8) 127));
    for (int k = 0; k < 10; ++k)
    {
        juce::AudioSourceChannelInfo info (&out, 0, blockSize);
        mixer.getNextAudioBlock (info);
    }
    const bool heldOn = sampler.isPlaying (3);
    midi.handleMessage (juce::MidiMessage::noteOff (10, 60));
    for (int k = 0; k < 4; ++k)
    {
        juce::AudioSourceChannelInfo info (&out, 0, blockSize);
        mixer.getNextAudioBlock (info);
    }
    check (heldOn && ! sampler.isPlaying (3), "MIDI: pad en hold suena con la nota pulsada y calla al soltarla", 0.0, 0.0);
}


// --- Automix -----------------------------------------------------------------------------

// Cancion sintetica de 50 s: un acorde suave (sin golpes) de 1 a 48 s y bombos de 'bpm'
// solo entre 8 s y 40 s (la parte ritmica). Antes de 1 s y despues de 48 s, silencio.
juce::AudioBuffer<float> songWithSections (double bpm, double beatsFrom = 8.0, double beatsTo = 40.0)
{
    const int n = (int) (50.0 * sr);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    for (int i = (int) (1.0 * sr); i < (int) (48.0 * sr); ++i)
        for (int ch = 0; ch < 2; ++ch)
            b.setSample (ch, i, 0.12f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * i / sr));
    const double period = 60.0 / bpm;
    for (double t = beatsFrom; t < beatsTo - 1e-9; t += period)
    {
        const int start = (int) std::lround (t * sr);
        for (int i = 0; i < (int) (0.15 * sr); ++i)
        {
            const double ph = i / sr;
            const float v = (float) (0.8 * std::exp (-ph * 30.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * ph));
            for (int ch = 0; ch < 2; ++ch)
                b.addSample (ch, start + i, v);
        }
    }
    return b;
}

midj::TempoAnalysis gridFor (double bpm, double firstBeatTime)
{
    const double period = 60.0 / bpm;
    return { bpm, std::fmod (firstBeatTime, period) };
}

void testMixPoints()
{
    const auto song = songWithSections (120.0);
    const auto p = midj::MixPointAnalyser::analyse (song, sr, gridFor (120.0, 8.0));
    check (std::abs (p.realStart - 1.0) < 0.01, "puntos: realStart donde empieza el sonido (1 s)", p.realStart, 1.0);
    check (std::abs (p.realEnd - 48.0) < 0.01, "puntos: realEnd donde acaba (48 s)", p.realEnd, 48.0);
    check (std::abs (p.tempoStart - 8.0) < 0.001, "puntos: tempoStart al empezar los bombos (8 s)", p.tempoStart, 8.0);
    check (std::abs (p.tempoEnd - 40.0) < 0.001, "puntos: tempoEnd al acabar el ultimo compas (40 s)", p.tempoEnd, 40.0);
    check (p.fadeStart >= p.realStart && p.fadeStart <= 8.1 && p.fadeEnd >= 39.4 && p.fadeEnd <= p.realEnd,
           "puntos: fadeStart y fadeEnd dentro de la parte con sonido", p.fadeEnd, 40.0);

    // Sin bombos no hay parte ritmica.
    const auto pad = songWithSections (120.0, 60.0, 60.0);
    const auto q = midj::MixPointAnalyser::analyse (pad, sr, gridFor (120.0, 0.0));
    check (! q.hasTempoSection(), "puntos: sin golpes no hay parte ritmica", q.tempoStart, -1.0);
}

struct AutomixRig
{
    juce::AudioFormatManager formats;
    midj::Deck a { formats }, b { formats };
    midj::Mixer mixer { a, b };
    midj::Automix automix { a, b, mixer };
    std::map<juce::String, std::pair<juce::AudioBuffer<float>, midj::TempoAnalysis>> songs;
    juce::AudioBuffer<float> out { 2, blockSize };

    AutomixRig()
    {
        mixer.prepareToPlay (blockSize, sr);
        a.setKeyLock (false);
        b.setKeyLock (false);
        automix.loadTrack = [this] (int deckIndex, const juce::File& file)
        {
            const auto it = songs.find (file.getFileName());
            if (it == songs.end())
                return false;
            (deckIndex == 0 ? a : b).loadBuffer (it->second.first, sr, file.getFileName(), it->second.second);
            return true;
        };
    }
    void addSong (const juce::String& name, juce::AudioBuffer<float> audio, midj::TempoAnalysis tempo)
    {
        songs[name] = { std::move (audio), tempo };
    }
    midj::Automix::Item item (const juce::String& name) { return { juce::File::getCurrentWorkingDirectory().getChildFile (name), name, {}, 0.0 }; }
    // Un bloque de audio y una llamada a update(), como el temporizador de la ventana.
    void step()
    {
        juce::AudioSourceChannelInfo info (&out, 0, blockSize);
        mixer.getNextAudioBlock (info);
        automix.update();
    }
};

void testAutomixQueueMove()
{
    AutomixRig rig;
    for (const auto* name : { "a.wav", "b.wav", "c.wav", "d.wav" })
        rig.automix.add (rig.item (name));
    const auto order = [&rig]
    {
        juce::String s;
        for (const auto& item : rig.automix.getQueue())
            s << item.title.upToFirstOccurrenceOf (".", false, false);
        return s;
    };
    rig.automix.move (2, 1);   // subir la c
    check (order() == "acbd", "subir: la tercera pasa a segunda", order() == "acbd" ? 1.0 : 0.0, 1.0);
    rig.automix.move (0, 3);   // arrastrar la primera al final
    check (order() == "cbda", "arrastrar al final", order() == "cbda" ? 1.0 : 0.0, 1.0);
    rig.automix.move (3, 0);   // y de vuelta al principio
    check (order() == "acbd", "arrastrar al principio", order() == "acbd" ? 1.0 : 0.0, 1.0);
    rig.automix.move (0, -1);
    rig.automix.move (3, 4);
    rig.automix.move (9, 0);
    check (order() == "acbd" && rig.automix.getQueue().size() == 4, "fuera de la lista: no cambia nada",
           (double) rig.automix.getQueue().size(), 4.0);
}

void testAutomixTempo()
{
    AutomixRig rig;
    rig.addSong ("a.wav", songWithSections (120.0), gridFor (120.0, 8.0));
    rig.addSong ("b.wav", songWithSections (124.0), gridFor (124.0, 8.0));
    rig.automix.loadTrack (0, rig.item ("a.wav").file);
    rig.a.setPositionSeconds (20.0);
    rig.a.play();
    rig.automix.add (rig.item ("b.wav"));
    rig.automix.setEnabled (true);

    // Plan: a tempo (124/120 = 1,033), 16 pulsos que acaban en el fin de la parte ritmica de A (40 s).
    rig.step();
    check (rig.automix.nextIsTempoMix() && std::abs (rig.automix.getPlannedStart() - 32.0) < 1e-6,
           "automix a tempo: empieza 16 pulsos antes del fin ritmico (32 s)", rig.automix.getPlannedStart(), 32.0);

    double startedAt = -1.0, worstPhase = 0.0, worstTempo = 0.0, bAtStart = 0.0;
    bool crossfaderMoved = false;
    for (int k = 0; k < (int) (26.0 * sr) / blockSize; ++k)
    {
        rig.step();
        const auto state = rig.automix.getState();
        if (state == midj::Automix::State::mixing && startedAt < 0.0)
        {
            startedAt = rig.a.getPositionSeconds();
            bAtStart = rig.b.getPositionSeconds();
        }
        if (state == midj::Automix::State::mixing && rig.automix.getProgress() > 0.1)
        {
            const double phase = std::abs (wrapPhase (rig.a.getBeatPhase() - rig.b.getBeatPhase()));
            worstPhase = std::max (worstPhase, phase);
            worstTempo = std::max (worstTempo, std::abs (rig.a.getCurrentBpm() / rig.b.getCurrentBpm() - 1.0));
            crossfaderMoved = crossfaderMoved || (rig.mixer.getCrossfader() > 0.3f && rig.mixer.getCrossfader() < 0.7f);
        }
    }
    check (std::abs (startedAt - 32.0) < 0.02, "automix a tempo: la transicion empieza a los 32 s", startedAt, 32.0);
    check (std::abs (bAtStart - 8.0) < 0.02, "automix a tempo: B entra en el inicio de su parte ritmica (compas)", bAtStart, 8.0);
    check (worstPhase < 0.03, "automix a tempo: pulsos alineados durante la mezcla (desfase en pulsos)", worstPhase, 0.0);
    check (worstTempo < 0.005, "automix a tempo: los dos al mismo tempo mientras se desliza", worstTempo * 100.0, 0.0);
    check (crossfaderMoved && rig.mixer.getCrossfader() == 1.0f, "automix: el crossfader pasa de A a B", rig.mixer.getCrossfader(), 1.0);
    check (! rig.a.isPlaying() && rig.b.isPlaying() && rig.b.getSpeed() == 1.0 && rig.automix.getCurrentDeck() == 1,
           "automix: al acabar A se para y B sigue a su propio tempo", rig.b.getSpeed(), 1.0);

    // Lista terminada: cuando B acaba, el automix se apaga solo.
    rig.b.setPositionSeconds (48.0);
    for (int k = 0; k < (int) (3.0 * sr) / blockSize; ++k)
        rig.step();
    check (! rig.automix.isEnabled(), "automix: al acabar la ultima cancion se apaga", 0.0, 0.0);
}

void testAutomixFadeAndMixNow()
{
    // B sin tempo (solo el acorde): fundido de 6 s que acaba en el fadeEnd de A.
    AutomixRig rig;
    rig.addSong ("a.wav", songWithSections (120.0), gridFor (120.0, 8.0));
    rig.addSong ("pad.wav", songWithSections (120.0, 60.0, 60.0), { 0.0, 0.0 });
    rig.automix.loadTrack (0, rig.item ("a.wav").file);
    rig.a.setPositionSeconds (30.0);
    rig.a.play();
    rig.automix.add (rig.item ("pad.wav"));
    rig.automix.setEnabled (true);
    rig.step();
    const double fadeEnd = rig.a.getMixPoints().fadeEnd;
    check (! rig.automix.nextIsTempoMix() && std::abs (rig.automix.getPlannedStart() - (fadeEnd - 6.0)) < 1e-6,
           "automix con fundido: empieza 6 s antes del fadeEnd de A", rig.automix.getPlannedStart(), fadeEnd - 6.0);
    double bAtStart = -1.0;
    for (int k = 0; k < (int) (20.0 * sr) / blockSize && bAtStart < 0.0; ++k)
    {
        rig.step();
        if (rig.automix.getState() == midj::Automix::State::mixing)
            bAtStart = rig.b.getPositionSeconds();
    }
    check (std::abs (bAtStart - rig.b.getMixPoints().fadeStart) < 0.02, "automix con fundido: B entra en su fadeStart",
           bAtStart, rig.b.getMixPoints().fadeStart);

    // Mezclar ya: la transicion empieza en el momento.
    AutomixRig rig2;
    rig2.addSong ("a.wav", songWithSections (120.0), gridFor (120.0, 8.0));
    rig2.addSong ("b.wav", songWithSections (122.0), gridFor (122.0, 8.0));
    rig2.automix.loadTrack (0, rig2.item ("a.wav").file);
    rig2.a.setPositionSeconds (12.0);
    rig2.a.play();
    rig2.automix.add (rig2.item ("b.wav"));
    rig2.automix.setEnabled (true);
    rig2.step();
    rig2.automix.mixNow();
    const double pressedAt = rig2.a.getPositionSeconds();
    rig2.step();
    check (rig2.automix.getState() == midj::Automix::State::mixing && std::abs (rig2.a.getPositionSeconds() - pressedAt) < 0.05,
           "automix: 'mezclar ya' empieza al momento", rig2.a.getPositionSeconds(), pressedAt);

    // La lista se guarda y se lee.
    midj::Automix::Item real { juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsAutomix.wav"),
                               "Titulo", "Artista", 120.0 };
    real.file.replaceWithText ("x");
    rig2.automix.add (real);
    const auto listFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsAutomix.xml");
    rig2.automix.saveQueue (listFile);
    rig2.automix.clearQueue();
    rig2.automix.loadQueue (listFile);
    check (rig2.automix.getQueue().size() == 1 && rig2.automix.getQueue()[0].title == "Titulo",
           "automix: la lista se guarda y se lee (solo archivos que existen)", (double) rig2.automix.getQueue().size(), 1.0);
    listFile.deleteFile();
    real.file.deleteFile();
}


// --- Stems con IA ------------------------------------------------------------------------

juce::File stemsModelFolder()
{
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("models").getChildFile ("spleeter-2stems");
}

void testStemStft()
{
    // La STFT de la separacion: con mascara 1 devuelve la entrada (si no tiene nada por encima de
    // 11 kHz, la parte que el modelo no ve), con 0 silencio y con 0,5 la mitad. En los 4096 primeros
    // y ultimos samples no se exige: el relleno reflejado de los bordes crea un pico con agudos por
    // encima de 11 kHz que la mascara (como en sherpa-onnx) quita.
    midj::StemSeparator sep;
    juce::AudioBuffer<float> in (2, (int) (30.0 * sr));   // 30 s: varios trozos de 512 fotogramas
    for (int i = 0; i < in.getNumSamples(); ++i)
    {
        in.setSample (0, i, 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr));
        in.setSample (1, i, 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 2500.0 * i / sr + 0.3));
    }
    juce::AudioBuffer<float> out;
    sep.separateWithMask (in, out, [] (int, int) { return 1.0f; });
    float worst = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 4096; i < in.getNumSamples() - 4096; ++i)
            worst = std::max (worst, std::abs (out.getSample (ch, i) - in.getSample (ch, i)));
    check (out.getNumSamples() == in.getNumSamples() && worst < 1e-4f, "stems STFT: mascara 1 = la entrada (error maximo)", worst, 0.0);
    sep.separateWithMask (in, out, [] (int, int) { return 0.0f; });
    check (out.getMagnitude (0, out.getNumSamples()) < 1e-6f, "stems STFT: mascara 0 = silencio", out.getMagnitude (0, out.getNumSamples()), 0.0);
    sep.separateWithMask (in, out, [] (int, int) { return 0.5f; });
    check (std::abs (out.getRMSLevel (0, 0, out.getNumSamples()) / in.getRMSLevel (0, 0, in.getNumSamples()) - 0.5f) < 1e-3f,
           "stems STFT: mascara 0,5 = la mitad", out.getRMSLevel (0, 0, out.getNumSamples()) / in.getRMSLevel (0, 0, in.getNumSamples()), 0.5);

    // Remuestreo 48 kHz -> 44,1 kHz -> 48 kHz: un seno de 1 kHz conserva su frecuencia.
    juce::AudioBuffer<float> at48 (2, 48000 * 3);
    for (int i = 0; i < at48.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            at48.setSample (ch, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / 48000.0));
    const auto at44 = midj::StemSeparator::resample (at48, 48000.0, 44100.0);
    check (std::abs (at44.getNumSamples() - 44100 * 3) <= 1 && std::abs (sineFrequency (at44, 2000, at44.getNumSamples() - 2000) - 1000.0) < 0.5,
           "stems: remuestreo a 44,1 kHz conserva el tono", sineFrequency (at44, 2000, at44.getNumSamples() - 2000), 1000.0);
}

void testStemModel()
{
    midj::StemSeparator sep;
    juce::String error;
    const bool loaded = sep.load (stemsModelFolder(), midj::StemSeparator::Device::cpu, error);
    check (loaded, ("stems: el modelo carga en CPU " + error).toRawUTF8(), loaded ? 1.0 : 0.0, 1.0);
    if (! loaded)
        return;
    std::printf ("[INFO]  %s\n[INFO]  metadatos: %s\n", sep.describe().toRawUTF8(), sep.getMetadata().toRawUTF8());

    // Solo bombos (sin voz): la voz que saca el modelo es poca.
    auto drums = toBuffer (clickTrack (120.0, 15.0, sr, 0.25));
    drums.setSize (2, drums.getNumSamples(), true);
    drums.copyFrom (1, 0, drums, 0, 0, drums.getNumSamples());
    juce::AudioBuffer<float> vocals;
    int calls = 0;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const bool ok = sep.separateVocals (drums, vocals, [&calls] (double) { ++calls; return true; });
    const double seconds = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
    bool finite = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < vocals.getNumSamples(); ++i)
            finite = finite && std::isfinite (vocals.getSample (ch, i));
    check (ok && finite && vocals.getNumSamples() == drums.getNumSamples() && calls == 2,
           "stems: separa 15 s (2 trozos, valores validos)", calls, 2.0);
    const double ratio = vocals.getRMSLevel (0, 0, vocals.getNumSamples()) / drums.getRMSLevel (0, 0, drums.getNumSamples());
    check (ratio < 0.3, "stems: en una pista de bombos casi no hay voz (proporcion)", ratio, 0.0);
    std::printf ("[INFO]  separar 15 s en CPU: %.1f s (%.2f x tiempo real)\n", seconds, seconds / 15.0);

    // Cancelar a mitad.
    const bool cancelled = ! sep.separateVocals (drums, vocals, [] (double p) { return p < 0.5; });
    check (cancelled, "stems: se puede cancelar a mitad", 1.0, 1.0);
}

void testDeckStems()
{
    // Cancion = "instrumental" (220 Hz) + "voz" (880 Hz); la voz separada es exactamente el 880.
    const int n = (int) (20.0 * sr);
    juce::AudioBuffer<float> song (2, n);
    auto voice = std::make_shared<juce::AudioBuffer<float>> (2, n);
    for (int i = 0; i < n; ++i)
        for (int ch = 0; ch < 2; ++ch)
        {
            const float inst = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr);
            const float v = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 880.0 * i / sr);
            song.setSample (ch, i, inst + v);
            voice->setSample (ch, i, v);
        }
    juce::AudioFormatManager formats;
    midj::Deck deck (formats);
    deck.prepareToPlay (blockSize, sr);
    deck.setKeyLock (false);
    deck.loadBuffer (song, sr, "stems", { 120.0, 0.0 });
    auto shortVoice = std::make_shared<juce::AudioBuffer<float>> (2, n - 10);
    check (! deck.setVocals ({}, shortVoice) && ! deck.hasStems(), "stems en el deck: una voz de otro tamano se rechaza", 0.0, 0.0);
    check (deck.setVocals ({}, voice) && deck.hasStems(), "stems en el deck: se aceptan", 1.0, 1.0);

    deck.play();
    auto out = renderOutput (deck, 1.0);
    float diff = 0.0f;
    for (int i = 0; i < out.getNumSamples(); ++i)
        diff = std::max (diff, std::abs (out.getSample (0, i) - song.getSample (0, i)));
    check (diff < 1e-5f, "stems: voz e instrumental encendidos = la cancion tal cual", diff, 0.0);

    deck.setVocalsOn (false);
    out = renderOutput (deck, 1.0);
    const double instOnly = sineFrequency (out, (int) (0.2 * sr), out.getNumSamples());
    check (std::abs (instOnly - 220.0) < 0.5 && maxStep (out, 1, out.getNumSamples()) < 0.08f,
           "stems: sin voz suena solo lo instrumental (220 Hz), sin clic", instOnly, 220.0);
    deck.setVocalsOn (true);
    deck.setInstrumentalOn (false);
    out = renderOutput (deck, 1.0);
    const double voiceOnly = sineFrequency (out, (int) (0.2 * sr), out.getNumSamples());
    check (std::abs (voiceOnly - 880.0) < 0.5, "stems: sin instrumental suena solo la voz (880 Hz)", voiceOnly, 880.0);

    deck.setKeyLock (true);
    deck.setInstrumentalOn (true);
    deck.setVocalsOn (false);
    deck.setSpeed (1.06);
    out = renderOutput (deck, 2.0);
    const double withKeylock = sineFrequency (out, (int) (0.5 * sr), out.getNumSamples());
    check (std::abs (withKeylock - 220.0) < 0.5, "stems con keylock +6 %: sin voz y con su tono (220 Hz)", withKeylock, 220.0);

    // Al cargar otra cancion los stems se olvidan y los dos vuelven a estar encendidos.
    deck.loadBuffer (song, sr, "otra", { 120.0, 0.0 });
    check (! deck.hasStems() && deck.isVocalsOn() && deck.isInstrumentalOn(), "stems: una cancion nueva empieza sin stems", 0.0, 0.0);
}

void testStemsService()
{
    // Servicio completo: separa un WAV, guarda la voz en la cache y la segunda vez la lee de ahi.
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsStems");
    dir.deleteRecursively();
    const auto song = dir.getChildFile ("cancion.wav");
    writeClickWav (song, 120.0, 14.0);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::shared_ptr<const juce::AudioBuffer<float>> got;
    {
        midj::StemsService service (formats, dir, stemsModelFolder());
        service.request (song, true);
        const auto deadline = juce::Time::getMillisecondCounter() + 180000;
        while (service.getStatus (song) != midj::StemsService::Status::ready
               && service.getStatus (song) != midj::StemsService::Status::failed && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep (100);
        check (service.getStatus (song) == midj::StemsService::Status::ready, "servicio de stems: separa la cancion", 1.0, 1.0);
        check (service.isCached (song), "servicio de stems: guarda la voz en la cache", 1.0, 1.0);
        const auto cache = service.cacheFileFor (song);
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (cache));
        check (r != nullptr && r->lengthInSamples == (juce::int64) (14.0 * sr) && r->numChannels == 2,
               "servicio de stems: la voz en FLAC mide lo mismo que la cancion", r ? (double) r->lengthInSamples : 0.0, 14.0 * sr);
        r.reset();

        // Otra vez: sale de la cache (mucho mas rapido que separar).
        const auto start = juce::Time::getMillisecondCounterHiRes();
        service.request (song, true);
        juce::Thread::sleep (50);
        while (service.getStatus (song) != midj::StemsService::Status::ready && juce::Time::getMillisecondCounterHiRes() - start < 30000)
            juce::Thread::sleep (20);
        const double ms = juce::Time::getMillisecondCounterHiRes() - start;
        check (ms < 3000.0, "servicio de stems: la segunda vez sale de la cache (ms)", ms, 0.0);

        // Si el archivo cambia, la cache ya no vale.
        writeClickWav (song, 100.0, 14.0);
        song.setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::minutes (2));
        check (! service.isCached (song), "servicio de stems: un archivo cambiado no usa la cache vieja", 0.0, 0.0);

        // Cerrar con una separacion a medias no se queda colgado.
        service.request (song, true);
        while (service.getStatus (song) != midj::StemsService::Status::working && juce::Time::getMillisecondCounterHiRes() - start < 60000)
            juce::Thread::sleep (20);
    }
    check (true, "servicio de stems: se cierra aunque este separando", 0.0, 0.0);
    dir.deleteRecursively();
}

// MiDJTests --stems cancion.mp3 [cpu|gpu] [segundos]: separa y guarda voz e instrumental.
int stemsFile (const juce::String& path, bool gpu, double seconds, const juce::File& outDir)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    double rate = 0.0;
    auto audio = readFile (formats, juce::File (path), rate);
    if (audio == nullptr)
        return 1;
    // Un fragmento de 'seconds' desde el minuto 1 (donde ya suele haber voz).
    const int from = std::min ((int) (60.0 * rate), juce::jmax (0, audio->getNumSamples() - (int) (seconds * rate)));
    const int count = std::min ((int) (seconds * rate), audio->getNumSamples() - from);
    juce::AudioBuffer<float> clip (2, count);
    for (int ch = 0; ch < 2; ++ch)
        clip.copyFrom (ch, 0, *audio, ch, from, count);

    midj::StemSeparator sep;
    juce::String error;
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    if (! sep.load (stemsModelFolder(), gpu ? midj::StemSeparator::Device::gpu : midj::StemSeparator::Device::cpu, error))
    {
        std::printf ("No se pudo cargar: %s\n", error.toRawUTF8());
        return 1;
    }
    std::printf ("%s\n  modelo: %s, carga %.1f s\n", juce::File (path).getFileName().toRawUTF8(), sep.describe().toRawUTF8(),
                 (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
    t0 = juce::Time::getMillisecondCounterHiRes();
    const auto at44 = midj::StemSeparator::resample (clip, rate, midj::StemSeparator::modelSampleRate);
    juce::AudioBuffer<float> vocals44;
    if (! sep.separateVocals (at44, vocals44))
    {
        std::printf ("Fallo la separacion: %s\n", sep.getLastError().toRawUTF8());
        return 1;
    }
    auto vocals = midj::StemSeparator::resample (vocals44, midj::StemSeparator::modelSampleRate, rate);
    vocals.setSize (2, count, true);
    const double took = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    juce::AudioBuffer<float> instrumental (2, count);
    for (int ch = 0; ch < 2; ++ch)
    {
        instrumental.copyFrom (ch, 0, clip, ch, 0, count);
        instrumental.addFrom (ch, 0, vocals, ch, 0, count, -1.0f);
    }
    std::printf ("  %.0f s de audio separados en %.1f s (%.2f x tiempo real)\n", count / rate, took, took / (count / rate));
    std::printf ("  nivel: original %.3f, voz %.3f, instrumental %.3f (RMS)\n", clip.getRMSLevel (0, 0, count),
                 vocals.getRMSLevel (0, 0, count), instrumental.getRMSLevel (0, 0, count));

    outDir.createDirectory();
    const auto base = juce::File (path).getFileNameWithoutExtension().substring (0, 24).trim();
    for (auto [suffix, buffer] : { std::pair<const char*, juce::AudioBuffer<float>*> { " - voz.wav", &vocals },
                                   std::pair<const char*, juce::AudioBuffer<float>*> { " - instrumental.wav", &instrumental } })
    {
        const auto file = outDir.getChildFile (base + suffix);
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), rate, 2, 16, {}, 0));
        if (w != nullptr)
            w->writeFromAudioSampleBuffer (*buffer, 0, count);
    }
    return 0;
}


// --- Video ------------------------------------------------------------------------------

// MP4 de prueba con Media Foundation (como el AVI de colores con que se probo VirtualDJ):
// H.264 de 'w' x 'h' a 25 fps con un color por segundo (rojo, verde, azul, amarillo, magenta)
// y AAC estereo a 44,1 kHz con un La de 440 Hz.
bool writeTestVideo (const juce::File& file, int seconds, int w = 320, int h = 240)
{
    using Microsoft::WRL::ComPtr;
    if (! midj::MediaFoundation::initialiseThisThread())
        return false;
    file.deleteFile();
    ComPtr<IMFSinkWriter> writer;
    if (FAILED (MFCreateSinkWriterFromURL (file.getFullPathName().toWideCharPointer(), nullptr, nullptr, &writer)))
        return false;
    constexpr UINT32 fps = 25, rate = 44100;

    ComPtr<IMFMediaType> vOut, vIn, aOut, aIn;
    MFCreateMediaType (&vOut);
    vOut->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
    vOut->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_H264);
    vOut->SetUINT32 (MF_MT_AVG_BITRATE, 1000000);
    vOut->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize (vOut.Get(), MF_MT_FRAME_SIZE, (UINT32) w, (UINT32) h);
    MFSetAttributeRatio (vOut.Get(), MF_MT_FRAME_RATE, fps, 1);
    MFSetAttributeRatio (vOut.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    DWORD videoStream = 0, audioStream = 0;
    if (FAILED (writer->AddStream (vOut.Get(), &videoStream)))
        return false;
    MFCreateMediaType (&vIn);
    vIn->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
    vIn->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    vIn->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    vIn->SetUINT32 (MF_MT_DEFAULT_STRIDE, (UINT32) (w * 4));
    MFSetAttributeSize (vIn.Get(), MF_MT_FRAME_SIZE, (UINT32) w, (UINT32) h);
    MFSetAttributeRatio (vIn.Get(), MF_MT_FRAME_RATE, fps, 1);
    MFSetAttributeRatio (vIn.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED (writer->SetInputMediaType (videoStream, vIn.Get(), nullptr)))
        return false;

    MFCreateMediaType (&aOut);
    aOut->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    aOut->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_AAC);
    aOut->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    aOut->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    aOut->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, 2);
    aOut->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000);
    if (FAILED (writer->AddStream (aOut.Get(), &audioStream)))
        return false;
    MFCreateMediaType (&aIn);
    aIn->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    aIn->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_PCM);
    aIn->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    aIn->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    aIn->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, 2);
    aIn->SetUINT32 (MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
    aIn->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * 4);
    if (FAILED (writer->SetInputMediaType (audioStream, aIn.Get(), nullptr)) || FAILED (writer->BeginWriting()))
        return false;

    const uint32_t colours[] = { 0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffff00, 0xffff00ff };
    auto writeSample = [&] (DWORD stream, const void* bytes, DWORD size, LONGLONG time, LONGLONG duration)
    {
        ComPtr<IMFMediaBuffer> buffer;
        ComPtr<IMFSample> sample;
        MFCreateMemoryBuffer (size, &buffer);
        BYTE* data = nullptr;
        buffer->Lock (&data, nullptr, nullptr);
        std::memcpy (data, bytes, size);
        buffer->Unlock();
        buffer->SetCurrentLength (size);
        MFCreateSample (&sample);
        sample->AddBuffer (buffer.Get());
        sample->SetSampleTime (time);
        sample->SetSampleDuration (duration);
        return SUCCEEDED (writer->WriteSample (stream, sample.Get()));
    };
    std::vector<uint32_t> frame ((size_t) w * (size_t) h);
    std::vector<int16_t> pcm ((size_t) rate / fps * 2);
    for (UINT32 f = 0; f < (UINT32) seconds * fps; ++f)
    {
        std::fill (frame.begin(), frame.end(), colours[(f / fps) % 5]);
        if (! writeSample (videoStream, frame.data(), (DWORD) (frame.size() * 4), (LONGLONG) f * 10000000 / fps, 10000000 / fps))
            return false;
        for (size_t i = 0; i < pcm.size() / 2; ++i)
        {
            const double t = (double) (f * (rate / fps) + i) / rate;
            const auto v = (int16_t) (12000.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t));
            pcm[2 * i] = pcm[2 * i + 1] = v;
        }
        if (! writeSample (audioStream, pcm.data(), (DWORD) (pcm.size() * 2), (LONGLONG) f * 10000000 / fps, 10000000 / fps))
            return false;
    }
    return SUCCEEDED (writer->Finalize());
}

// Color dominante del centro de una imagen: 'R', 'G', 'B', 'Y' (rojo + verde) o 'M' (rojo + azul).
char dominantColour (const juce::Image& image)
{
    const auto c = image.getPixelAt (image.getWidth() / 2, image.getHeight() / 2);
    const bool r = c.getRed() > 160, g = c.getGreen() > 160, b = c.getBlue() > 160;
    if (r && g && ! b) return 'Y';
    if (r && b && ! g) return 'M';
    if (r && ! g && ! b) return 'R';
    if (g && ! r && ! b) return 'G';
    if (b && ! r && ! g) return 'B';
    return '?';
}

void testVideo()
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsVideo.mp4");
    check (writeTestVideo (file, 5), "video: se crea un MP4 de prueba (H.264 + AAC)", 1.0, 1.0);

    juce::String error;
    auto video = midj::VideoDecoder::open (file, 160, error);
    check (video != nullptr, ("video: Media Foundation lo abre " + error).toRawUTF8(), 1.0, 1.0);
    if (video == nullptr)
        return;
    check (video->getWidth() == 160 && video->getHeight() == 120 && std::abs (video->getFrameRate() - 25.0) < 0.01
               && std::abs (video->getDuration() - 5.0) < 0.1,
           "video: 160x120 (reducido), 25 fps, 5 s", video->getWidth(), 160);
    std::printf ("        (este video se decodifica en la %s)\n", video->isUsingGpu() ? "GPU" : "CPU");

    // El camino de CPU (el de reserva si la GPU falla) da lo mismo.
    {
        juce::String cpuError;
        auto cpuVideo = midj::VideoDecoder::open (file, 160, cpuError, false);
        juce::Image frame;
        juce::String colours;
        if (cpuVideo != nullptr)
            for (double t : { 0.5, 1.5, 2.5, 3.5, 4.5 })
                colours += cpuVideo->frameAt (t, frame) ? juce::String::charToString (dominantColour (frame)) : "-";
        check (cpuVideo != nullptr && ! cpuVideo->isUsingGpu() && colours == "RGBYM",
               ("video: en la CPU tambien (" + colours + ")").toRawUTF8(), 0.0, 0.0);
    }

    juce::Image image;
    juce::String seen;
    for (double t : { 0.5, 1.5, 2.5, 3.5, 4.5 })   // hacia delante, seguido
        seen += video->frameAt (t, image) ? juce::String::charToString (dominantColour (image)) : "-";
    check (seen == "RGBYM", ("video: un color por segundo en orden (" + seen + ")").toRawUTF8(), 0.0, 0.0);
    const bool backOk = video->frameAt (0.2, image) && dominantColour (image) == 'R'
                     && video->frameAt (3.9, image) && dominantColour (image) == 'Y';
    check (backOk, "video: saltar atras y adelante da el fotograma correcto", 0.0, 0.0);
    // Salto con limite de fotogramas: entrega uno anterior enseguida y alcanza en las vueltas siguientes.
    video->frameAt (0.0, image);
    bool catchUpOk = video->frameAt (4.6, image, 1) && video->getCurrentFrameTime() < 4.6 && video->isCatchingUp();
    int rounds = 0;
    while (catchUpOk && video->isCatchingUp() && rounds++ < 200)
        catchUpOk = video->frameAt (4.6, image, 2);
    check (catchUpOk && ! video->isCatchingUp() && std::abs (video->getCurrentFrameTime() - 4.6) < 1e-3 && dominantColour (image) == 'M',
           "video: tras un salto avanza por partes hasta alcanzar (4,6 s = fotograma 115)", video->getCurrentFrameTime(), 4.6);
    const bool exactOk = video->frameAt (1.04, image);
    check (exactOk && std::abs (video->getCurrentFrameTime() - 1.04) < 1e-3,
           "video: fotograma exacto (1,04 s = fotograma 26)", video->getCurrentFrameTime(), 1.04);

    // El sonido del MP4 entra en un deck como cualquier cancion.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    formats.registerFormat (new midj::MediaFoundationAudioFormat(), false);
    double rate = 0.0;
    auto audio = readFile (formats, file, rate);
    check (audio != nullptr && rate == 44100.0 && std::abs (audio->getNumSamples() / rate - 5.0) < 0.1,
           "video: su audio se lee (44,1 kHz, 5 s)", audio ? audio->getNumSamples() / rate : 0.0, 5.0);
    if (audio != nullptr)
        check (std::abs (sineFrequency (*audio, (int) (1.0 * rate), (int) (4.0 * rate)) - 440.0) < 0.5,
               "video: el audio es el La de 440 Hz", sineFrequency (*audio, (int) (1.0 * rate), (int) (4.0 * rate)), 440.0);

    // Lectura con saltos (lo que hace un lector al pedir trozos sueltos).
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    juce::AudioBuffer<float> part (2, 4410);
    bool same = reader != nullptr && audio != nullptr;
    if (same)
        for (juce::int64 start : { (juce::int64) 88200, (juce::int64) 22050, (juce::int64) 150000 })
        {
            reader->read (&part, 0, 4410, start, true, true);
            for (int i = 0; i < 4410 && same; ++i)
                same = std::abs (part.getSample (0, i) - audio->getSample (0, (int) start + i)) < 1e-4f;
        }
    check (same, "video: leer trozos sueltos da lo mismo que leer seguido", 0.0, 0.0);
    reader.reset();

    // El video de un deck sigue la posicion del deck (saltos adelante y atras).
    {
        midj::Deck deck (formats);
        deck.loadBuffer (juce::AudioBuffer<float> (2, 5 * 44100), 44100.0, "video", { 120.0, 0.0 });
        midj::DeckVideo deckVideo (deck, 160);
        deckVideo.setFile (file);
        juce::Thread::sleep (300);
        check (! deckVideo.getFrame().isValid() && ! deckVideo.hasVideo(), "video: sin ventana abierta no se decodifica", 0.0, 0.0);
        deckVideo.setActive (true);
        auto colourAt = [&] (double seconds)
        {
            deck.setPositionSeconds (seconds);
            for (int i = 0; i < 200; ++i)   // hasta 2 s
            {
                juce::Thread::sleep (10);
                const auto frame = deckVideo.getFrame();
                if (frame.isValid() && std::abs (deckVideo.getFrameTime() - std::floor (seconds * 25.0) / 25.0) < 1e-3)
                    return dominantColour (frame);
            }
            return '-';
        };
        juce::String followed;
        for (double t : { 2.5, 0.5, 4.5, 1.5 })
            followed += juce::String::charToString (colourAt (t));
        check (deckVideo.hasVideo() && followed == "BRMG", ("video: el video del deck sigue su posicion (" + followed + ")").toRawUTF8(), 0.0, 0.0);
        deckVideo.setFile ({});
        for (int i = 0; i < 200 && (deckVideo.hasVideo() || deckVideo.getFrame().isValid()); ++i)   // hasta 2 s (con el equipo cargado tarda mas)
            juce::Thread::sleep (10);
        check (! deckVideo.hasVideo() && ! deckVideo.getFrame().isValid(), "video: cancion sin video = sin imagen", 0.0, 0.0);
    }
    file.deleteFile();
}

juce::Image solidImage (int w, int h, juce::Colour colour)
{
    juce::Image image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    image.clear (image.getBounds(), colour);
    return image;
}

void testCompositor()
{
    using midj::Compositor;
    const auto fitted = Compositor::fitInside (640.0f, 480.0f, { 1280.0f, 720.0f });
    check (fitted == juce::Rectangle<float> (160.0f, 0.0f, 960.0f, 720.0f), "compositor: 4:3 en 16:9 con bandas a los lados", fitted.getX(), 160.0);

    Compositor compositor ({ 1280, 720, 30, true });
    check (compositor.isReady(), ("compositor: Direct3D 11 listo " + compositor.getError()).toRawUTF8(), 1.0, 1.0);
    if (! compositor.isReady())
        return;
    const auto stats = compositor.getStats();
    std::printf ("        (en %s: %s)\n", stats.hardware ? "la GPU" : "WARP", stats.adapter.toRawUTF8());

    auto pixel = [] (const juce::Image& image, int x, int y) { return image.isValid() ? image.getPixelAt (x, y) : juce::Colour(); };

    // Una imagen 4:3 sin area: encajada en el centro, negro a los lados.
    Compositor::Layer red;
    red.slot = 0;
    red.image = solidImage (160, 120, juce::Colours::red);
    red.version = 1;
    compositor.renderOnce ({ red });
    auto program = compositor.readProgram();
    const auto centre = pixel (program, 640, 360), side = pixel (program, 80, 360);
    check (program.getWidth() == 1280 && centre.getRed() > 250 && centre.getGreen() < 5 && side.getRed() < 5 && side.getBrightness() < 0.02f,
           "compositor: la capa se ve en el centro y bandas negras", centre.getRed(), 255.0);

    // Fundido a la mitad en luz lineal (como OBS): blanco y negro al 50 % = 188, no 128.
    Compositor::Layer white, black;
    white.slot = 1;
    white.image = solidImage (64, 36, juce::Colours::white);
    white.version = 1;
    black.slot = 2;
    black.image = solidImage (64, 36, juce::Colours::black);
    black.version = 1;
    black.opacity = 0.5f;
    compositor.renderOnce ({ white, black });
    program = compositor.readProgram();
    const int mid = pixel (program, 640, 360).getRed();
    check (std::abs (mid - 188) <= 2, "compositor: fundido 50 % en luz lineal (188)", mid, 188.0);

    // Imagen semitransparente de JUCE (premultiplicada): blanco al 50 % sobre negro = lo mismo.
    Compositor::Layer translucent;
    translucent.slot = 3;
    translucent.image = solidImage (64, 36, juce::Colours::white.withAlpha (0.5f));
    translucent.version = 1;
    compositor.renderOnce ({ translucent });
    const int half = pixel (compositor.readProgram(), 640, 360).getRed();
    check (std::abs (half - 188) <= 3, "compositor: alfa premultiplicado de JUCE", half, 188.0);

    // Un area concreta y una nueva version de la misma ranura (se vuelve a subir).
    red.area = { 0.0f, 0.0f, 640.0f, 360.0f };
    red.image = solidImage (160, 120, juce::Colours::blue);
    red.version = 2;
    compositor.renderOnce ({ red });
    program = compositor.readProgram();
    check (pixel (program, 320, 180).getBlue() > 250 && pixel (program, 960, 540).getBrightness() < 0.02f,
           "compositor: capa en su area y la imagen nueva se sube", pixel (program, 320, 180).getBlue(), 255.0);

    // Una salida (ventana) y el hilo a ritmo fijo: unos 30 fotogramas en un segundo.
    HWND window = CreateWindowExW (WS_EX_TOOLWINDOW, L"STATIC", L"MiDJ prueba", WS_POPUP, 0, 0, 320, 180, nullptr, nullptr, nullptr, nullptr);
    compositor.addOutput (window);
    std::atomic<int> builds { 0 };
    compositor.buildFrame = [&] (Compositor::Frame& frame)
    {
        ++builds;
        frame.layers.push_back (white);
    };
    const auto before = compositor.getStats().frames;
    compositor.start();
    juce::Thread::sleep (1000);
    compositor.stop();
    const auto after = compositor.getStats();
    const auto done = (double) (after.frames - before);
    check (done >= 27 && done <= 33 && builds.load() == (int) done, "compositor: 30 fps en el hilo (fotogramas en 1 s)", done, 30.0);
    std::printf ("        (%.2f ms por fotograma, %llu perdidos)\n", after.renderMs, (unsigned long long) after.lagged);
    check (compositor.getNumOutputs() == 1, "compositor: la salida sigue registrada", compositor.getNumOutputs(), 1.0);
    compositor.removeOutput (window);
    DestroyWindow (window);
    check (compositor.getNumOutputs() == 0, "compositor: salida quitada", compositor.getNumOutputs(), 0.0);
}

void testScenes()
{
    using namespace midj;
    // Coleccion: por defecto la mezcla a pantalla completa, y vuelta completa por XML.
    auto c = SceneCollection::createDefault (1280, 720);
    check (c.scenes.size() == 1 && c.scenes[0].items.size() == 1 && c.scenes[0].items[0].type == SourceType::decks,
           "escenas: por defecto una con la mezcla de decks", (double) c.scenes.size(), 1.0);
    auto& second = c.addScene ("Fin");
    auto text = c.makeItem (SourceType::text, 1280, 720);
    text.text = "Ahora: {titulo}\n{bpm} BPM";
    text.rotation = 15.0f;
    text.cropLeft = 4.0f;
    second.items.push_back (text);
    second.items.push_back (c.makeItem (SourceType::colour, 1280, 720));
    c.transition = { TransitionType::slide, 900 };
    c.currentScene = second.id;
    const auto xml = c.toXml();
    auto back = SceneCollection::fromXml (*xml, 1280, 720);
    const auto* s = back.findScene (second.id);
    check (s != nullptr && s->items.size() == 2 && s->items[0].text == text.text && s->items[0].rotation == 15.0f
               && s->items[0].cropLeft == 4.0f && back.transition.type == TransitionType::slide && back.transition.milliseconds == 900
               && back.currentScene == second.id && back.newId() > text.id,
           "escenas: se guardan y se leen en XML", s != nullptr ? (double) s->items.size() : 0.0, 2.0);

    NowPlaying now { "La Maria", "Luister La Voz", 116.0 };
    check (SceneGeometry::resolveText ("{artista} - {titulo} ({bpm})", now) == "Luister La Voz - La Maria (116.0)",
           "escenas: {artista}, {titulo} y {bpm} en los textos", 0.0, 0.0);
    NowPlaying yt;
    yt.youtubeTitle = "Vallenato en vivo";
    yt.youtubeChannel = "Canal Oficial";
    check (SceneGeometry::resolveText ("En YouTube: {yt_titulo} ({yt_canal})", yt) == "En YouTube: Vallenato en vivo (Canal Oficial)",
           "escenas: {yt_titulo} y {yt_canal} en los textos", 0.0, 0.0);
    check (SceneGeometry::resolveText ("{artista} - {titulo}", { "Solo titulo", "", 0.0 }) == "Solo titulo"
               && SceneGeometry::resolveText ("{artista} - {titulo}", {}).isEmpty(),
           "escenas: sin artista no quedan guiones sueltos", 0.0, 0.0);

    // Geometria: recuadro con recorte y escala, giro y clic dentro.
    SceneItem item;
    item.position = { 100.0f, 50.0f };
    item.scaleX = item.scaleY = 2.0f;
    item.cropLeft = 10.0f;
    item.cropRight = 10.0f;
    const auto b = SceneGeometry::box (item, { 120, 60 });
    check (b == juce::Rectangle<float> (100.0f, 50.0f, 200.0f, 120.0f), "escenas: recuadro = (fuente - recorte) x escala", b.getWidth(), 200.0);
    Compositor::Layer layer;
    SceneGeometry::place (layer, item, { 120, 60 }, 120.0f, 60.0f);
    check (std::abs (layer.uv.getX() - 10.0f / 120.0f) < 1e-5f && std::abs (layer.uv.getWidth() - 100.0f / 120.0f) < 1e-5f
               && layer.area == b,
           "escenas: el recorte es la parte de la imagen que se ve", layer.uv.getX(), 10.0 / 120.0);
    item.rotation = 90.0f;   // de pie: 120 de ancho y 200 de alto alrededor del centro (200, 110)
    check (SceneGeometry::contains (item, { 120, 60 }, { 200.0f, 30.0f }) && ! SceneGeometry::contains (item, { 120, 60 }, { 120.0f, 110.0f }),
           "escenas: clic dentro de un elemento girado", 0.0, 0.0);

    // En la GPU: un rectangulo girado 90 grados queda de pie.
    Compositor compositor ({ 1280, 720, 30, true });
    if (! compositor.isReady())
        return;
    Compositor::Layer bar;
    bar.image = solidImage (4, 4, juce::Colours::white);
    bar.version = 1;
    bar.area = { 440.0f, 310.0f, 400.0f, 100.0f };
    bar.rotation = 90.0f;
    compositor.renderOnce ({ bar });
    auto img = compositor.readProgram();
    check (img.getPixelAt (640, 200).getRed() > 250 && img.getPixelAt (800, 360).getRed() < 5,
           "escenas: capa girada 90 grados en la GPU", img.getPixelAt (640, 200).getRed(), 255.0);

    // Transiciones: dos escenas (roja y azul) en sus texturas y mezcladas en el programa.
    Compositor::Frame frame;
    Compositor::Layer red, blue;
    red.image = solidImage (4, 4, juce::Colours::red);
    red.slot = 1;
    red.version = 1;
    red.area = { 0.0f, 0.0f, 1280.0f, 720.0f };
    blue = red;
    blue.image = solidImage (4, 4, juce::Colours::blue);
    blue.slot = 2;
    frame.scenes[0] = { red };
    frame.scenes[1] = { blue };
    frame.useScene = { true, true };
    VideoProgram::transitionLayers (TransitionType::fade, 0.5f, 1280.0f, 720.0f, frame.layers);
    compositor.renderOnce (frame);
    img = compositor.readProgram();
    const auto mid = img.getPixelAt (640, 360);
    check (std::abs (mid.getRed() - 188) <= 2 && std::abs (mid.getBlue() - 188) <= 2, "escenas: fundido a la mitad (en luz lineal)", mid.getRed(), 188.0);
    frame.layers.clear();
    VideoProgram::transitionLayers (TransitionType::slide, 0.5f, 1280.0f, 720.0f, frame.layers);
    compositor.renderOnce (frame);
    img = compositor.readProgram();
    check (img.getPixelAt (320, 360).getRed() > 250 && img.getPixelAt (960, 360).getBlue() > 250,
           "escenas: deslizar a la mitad = vieja a la izquierda, nueva a la derecha", img.getPixelAt (960, 360).getBlue(), 255.0);
    frame.layers.clear();
    VideoProgram::transitionLayers (TransitionType::fadeToBlack, 0.5f, 1280.0f, 720.0f, frame.layers);
    compositor.renderOnce (frame);
    check (compositor.readProgram().getPixelAt (640, 360).getBrightness() < 0.02f, "escenas: fundido a negro pasa por negro", 0.0, 0.0);

    // Barrido con imagen: con una mascara de negro (izquierda) a blanco (derecha) a la mitad, la
    // escena nueva (azul) ya cubre la izquierda y la vieja (roja) sigue a la derecha.
    {
        juce::Image gradient (juce::Image::ARGB, 64, 8, false);
        for (int x = 0; x < 64; ++x)
            for (int y = 0; y < 8; ++y)
            {
                const auto v = (juce::uint8) (x * 4);
                gradient.setPixelAt (x, y, juce::Colour (v, v, v));
            }
        frame.layers.clear();
        Compositor::Layer out, in;
        out.source = 0;
        in.source = 1;
        out.area = in.area = { 0.0f, 0.0f, 1280.0f, 720.0f };
        in.mask = gradient;
        in.maskVersion = 1;
        in.maskSlot = 901;
        in.maskProgress = 0.5f;
        in.maskSoftness = 0.05f;
        frame.layers = { out, in };
        compositor.renderOnce (frame);
        img = compositor.readProgram();
        check (img.getPixelAt (160, 360).getBlue() > 250 && img.getPixelAt (160, 360).getRed() < 5
                   && img.getPixelAt (1120, 360).getRed() > 250 && img.getPixelAt (1120, 360).getBlue() < 5,
               "transiciones: barrido con imagen a la mitad", img.getPixelAt (160, 360).getBlue(), 255.0);
    }

    // Video con mate: el color de la mitad izquierda donde el mate (mitad derecha) es blanco; donde
    // es negro se ve lo de abajo. Aqui el mate deja ver la mitad izquierda del rojo.
    {
        juce::Image matted (juce::Image::ARGB, 64, 16, false);
        for (int x = 0; x < 64; ++x)
            for (int y = 0; y < 16; ++y)
                matted.setPixelAt (x, y, x < 32 ? juce::Colours::red : x < 48 ? juce::Colours::white : juce::Colours::black);
        Compositor::Layer under, over;
        under.slot = 904;
        under.image = solidImage (16, 9, juce::Colours::lime);
        under.version = 1;
        under.area = over.area = { 0.0f, 0.0f, 1280.0f, 720.0f };
        over.slot = 905;
        over.image = matted;
        over.version = 1;
        over.matte = true;
        frame.layers = { under, over };
        compositor.renderOnce (frame);
        img = compositor.readProgram();
        const auto seen = img.getPixelAt (320, 360), hidden = img.getPixelAt (960, 360);
        check (seen.getRed() > 250 && seen.getGreen() < 5 && hidden.getGreen() > 250 && hidden.getRed() < 5,
               "video con mate: el color donde el mate es blanco, lo de abajo donde es negro", seen.getRed(), 255.0);
    }

    // El programa: escena con un color, cambio de escena con transicion y despues sin ella.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    Deck da (formats), db (formats);
    Mixer mixer (da, db);
    DeckVideo va (da, 160), vb (db, 160);
    VideoMix mix ({ &va, &vb }, { &da, &db }, mixer);
    VideoProgram videoProgram (mix, 1280, 720);
    auto col = SceneCollection::createDefault (1280, 720);
    auto& green = col.addScene ("Verde");
    auto fill = col.makeItem (SourceType::colour, 1280, 720);
    fill.colour = juce::Colours::lime;
    fill.width = 1280;
    fill.height = 720;
    fill.position = {};
    green.items.push_back (fill);
    col.transition = { TransitionType::fade, 300 };
    videoProgram.setScenes (col);
    Compositor::Frame f;
    videoProgram.buildFrame (f);
    check (f.layers.size() == 1 && ! f.useScene[0] && f.layers[0].slot == 4, "escenas: al aire la de la mezcla (logo)", (double) f.layers.size(), 1.0);
    videoProgram.switchTo (green.id);
    f.clear();
    videoProgram.buildFrame (f);
    check (videoProgram.isInTransition() && f.useScene[0] && f.useScene[1] && f.scenes[1].size() == 1 && f.layers.size() == 2,
           "escenas: al cambiar, transicion con las dos escenas", (double) f.layers.size(), 2.0);
    juce::Thread::sleep (350);
    f.clear();
    videoProgram.buildFrame (f);
    compositor.renderOnce (f);
    const auto g = compositor.readProgram().getPixelAt (100, 100);
    check (! videoProgram.isInTransition() && ! f.useScene[1] && g.getGreen() > 250 && g.getRed() < 5,
           "escenas: acabada la transicion, la escena verde", g.getGreen(), 255.0);

    // Transicion propia "a color": a la mitad, la pantalla es del color (blanco); al acabar, la escena nueva.
    {
        CustomTransition white;
        white.id = col.newId();
        white.name = "Blanco";
        white.kind = CustomTransitionKind::colour;
        white.colour = juce::Colours::white;
        white.milliseconds = 600;
        col.customTransitions.push_back (white);
        col.transition.custom = white.id;
        videoProgram.setScenes (col);
        const int mezcla = col.scenes.front().id;
        videoProgram.switchTo (mezcla);
        juce::Thread::sleep (280);   // cerca de la mitad
        f.clear();
        videoProgram.buildFrame (f);
        compositor.renderOnce (f);
        const auto w = compositor.readProgram().getPixelAt (100, 100);
        check (videoProgram.isInTransition() && w.getRed() > 230 && w.getBlue() > 230, "transiciones: a color pasa por el color",
               w.getRed(), 255.0);
        juce::Thread::sleep (400);
        f.clear();
        videoProgram.buildFrame (f);
        check (! videoProgram.isInTransition() && videoProgram.getCurrentScene() == mezcla, "transiciones: a color termina en la escena nueva", 0.0, 0.0);
        // Guardar y leer: la transicion propia y que esta elegida.
        const auto saved = SceneCollection::fromXml (*col.toXml(), 1280, 720);
        const auto* restored = saved.currentCustom();
        check (restored != nullptr && restored->kind == CustomTransitionKind::colour && restored->colour == juce::Colours::white
                   && restored->milliseconds == 600,
               "transiciones: la propia se guarda en el XML", restored != nullptr ? restored->milliseconds : 0, 600.0);
        // Stinger: su video encima; debajo la escena vieja hasta el punto de cambio y luego la nueva.
        struct FakeStinger : FrameSource
        {
            void show (const juce::Image& image) { publish (image); }
            juce::String getStatus() const override { return {}; }
            void update (const SceneItem&) override {}
            void setOnAir (bool) override {}
            void setViewing (bool) override {}
        };
        auto fake = std::make_shared<FakeStinger>();
        fake->show (solidImage (16, 9, juce::Colours::orange));
        videoProgram.setStinger (fake);
        CustomTransition sting;
        sting.id = col.newId();
        sting.kind = CustomTransitionKind::stinger;
        sting.milliseconds = 600;
        sting.cutMilliseconds = 300;
        col.customTransitions.push_back (sting);
        col.transition.custom = sting.id;
        videoProgram.setScenes (col);
        videoProgram.switchTo (green.id);
        juce::Thread::sleep (100);
        f.clear();
        videoProgram.buildFrame (f);
        const bool topIsStinger = ! f.layers.empty() && f.layers.back().slot == 902;
        const bool underIsOld = f.layers.size() >= 2 && f.layers.front().slot != 1000 + fill.id;
        juce::Thread::sleep (300);
        f.clear();
        videoProgram.buildFrame (f);
        const bool underIsNew = f.layers.size() >= 2 && f.layers.front().slot == 1000 + fill.id;
        check (topIsStinger && underIsOld && underIsNew, "transiciones: stinger encima y cambio de escena en su punto", 0.0, 0.0);
        juce::Thread::sleep (300);
        videoProgram.setStinger (nullptr);
        col.transition.custom = 0;
        videoProgram.setScenes (col);

        // Probar un barrido: de ella a ella misma no se veria; va a negro y vuelve a la escena.
        CustomTransition wipe;
        wipe.kind = CustomTransitionKind::luma;
        wipe.milliseconds = 300;
        videoProgram.previewTransition (wipe);
        const bool goingOut = videoProgram.isInTransition() && videoProgram.getCurrentScene() == green.id;
        juce::Thread::sleep (330);   // acabada la ida
        f.clear();
        videoProgram.buildFrame (f);   // empieza la vuelta
        const bool returning = videoProgram.isInTransition() && videoProgram.getCurrentScene() == green.id;
        juce::Thread::sleep (330);
        f.clear();
        videoProgram.buildFrame (f);
        check (goingOut && returning && ! videoProgram.isInTransition() && videoProgram.getCurrentScene() == green.id,
               "transiciones: probar un barrido va a negro y vuelve a la escena", 0.0, 0.0);
    }

    // Fuente de imagen: un PNG de 200x100 en el centro, a su tamano.
    const auto png = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsLogo.png");
    {
        png.deleteFile();
        juce::FileOutputStream out (png);
        juce::PNGImageFormat().writeImageToStream (solidImage (200, 100, juce::Colours::magenta), out);
    }
    auto& logoScene = col.addScene ("Logo");
    auto pic = col.makeItem (SourceType::image, 1280, 720);
    pic.file = png;
    pic.position = { 540.0f, 310.0f };
    logoScene.items.push_back (pic);
    col.transition = { TransitionType::cut, 0 };
    videoProgram.setScenes (col);
    videoProgram.switchTo (logoScene.id);
    f.clear();
    videoProgram.buildFrame (f);
    compositor.renderOnce (f);
    img = compositor.readProgram();
    const auto natural = videoProgram.naturalSize (pic);
    check (natural == juce::Point<int> (200, 100) && img.getPixelAt (640, 360) == juce::Colours::magenta
               && img.getPixelAt (520, 360).getBrightness() < 0.02f,
           "escenas: imagen PNG a su tamano donde se coloca", natural.x, 200.0);
    png.deleteFile();

    // Un texto agrandado se dibuja a la escala a la que se ve (no se estira ni se pixela) y su
    // lienzo no cambia: al triple, la imagen tiene el triple de pixeles y el recuadro es el mismo.
    {
        auto label = col.makeItem (SourceType::text, 1280, 720);
        label.text = "MiDJ";
        label.fontSize = 48.0f;
        const auto plain = SceneGeometry::renderText (label, label.text);
        label.scaleX = label.scaleY = 3.0f;
        const float density = SceneGeometry::textDensity (label);
        const auto sharp = SceneGeometry::renderText (label, label.text, density);
        col.addScene ("Texto").items.push_back (label);
        videoProgram.setScenes (col);
        const auto canvas = videoProgram.naturalSize (label);
        check (density == 3.0f && std::abs (sharp.getWidth() - 3 * plain.getWidth()) <= 3
                   && std::abs (canvas.x - plain.getWidth()) <= 1 && std::abs (canvas.y - plain.getHeight()) <= 1,
               "escenas: texto agrandado dibujado a su escala, mismo lienzo", sharp.getWidth(), 3.0 * plain.getWidth());
    }
}


// Una fuente de la GPU de prueba: 100x100 azul sin alfa (como el escritorio) con un "puntero"
// blanco de 10x10 en (50, 50).
struct FakeGpuSource : midj::Compositor::GpuSource
{
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> image, cursor;
    int updates = 0;

    static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> make (ID3D11Device* device, int size, uint32_t pixel)
    {
        std::vector<uint32_t> pixels ((size_t) (size * size), pixel);
        D3D11_TEXTURE2D_DESC td {};
        td.Width = td.Height = (UINT) size;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init { pixels.data(), (UINT) size * 4, 0 };
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        if (SUCCEEDED (device->CreateTexture2D (&td, &init, &texture)))
            device->CreateShaderResourceView (texture.Get(), nullptr, &view);
        return view;
    }

    Picture update (void* device, void*) override
    {
        ++updates;
        if (image == nullptr)
        {
            image = make ((ID3D11Device*) device, 100, 0x000000ffu);   // azul con alfa 0
            cursor = make ((ID3D11Device*) device, 10, 0xffffffffu);
        }
        Picture p;
        p.view = image.Get();
        p.width = p.height = 100;
        p.opaque = true;
        p.cursorView = cursor.Get();
        p.cursor = { 50.0f, 50.0f, 10.0f, 10.0f };
        return p;
    }
    juce::Point<int> getSize() const override { return { 100, 100 }; }
};

LRESULT CALLBACK testWindowProc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcW (hwnd, msg, wp, lp);
}

void pumpMessages()
{
    MSG msg;
    while (PeekMessageW (&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage (&msg);
        DispatchMessageW (&msg);
    }
}

void testCapture()
{
    using namespace midj;
    CoInitializeEx (nullptr, COINIT_MULTITHREADED);   // WinRT (si ya estaba en otro modo, vale igual)

    // Guardar y leer los elementos de captura.
    {
        SceneCollection col = SceneCollection::createDefault (1280, 720);
        auto s = col.makeItem (SourceType::screen, 1280, 720);
        s.monitor = 1;
        s.monitorId = "\\\\.\\DISPLAY2";
        s.cursor = false;
        auto w = col.makeItem (SourceType::window, 1280, 720);
        w.windowTitle = "Sin titulo - Bloc de notas";
        w.windowClass = "Notepad";
        w.windowExe = "notepad.exe";
        w.clientArea = false;
        col.scenes[0].items.push_back (s);
        col.scenes[0].items.push_back (w);
        const auto back = SceneCollection::fromXml (*col.toXml(), 1280, 720);
        const auto& items = back.scenes[0].items;
        check (items.size() == 3 && items[1].type == SourceType::screen && items[1].monitor == 1 && items[1].monitorId == "\\\\.\\DISPLAY2"
                   && ! items[1].cursor && items[2].type == SourceType::window && items[2].windowTitle == w.windowTitle
                   && items[2].windowClass == "Notepad" && items[2].windowExe == "notepad.exe" && ! items[2].clientArea && items[2].cursor,
               "captura: pantalla y ventana se guardan y se leen", (double) items.size(), 3.0);
    }

    // Pantallas: se encuentran por nombre y, si no esta, por numero.
    const auto monitors = ScreenCapture::monitors();
    check (! monitors.empty(), "captura: hay pantallas", (double) monitors.size(), 1.0);
    if (monitors.empty())
        return;
    check (ScreenCapture::findMonitor (monitors, monitors.back().id, 0) == (int) monitors.size() - 1
               && ScreenCapture::findMonitor (monitors, "\\\\.\\NOEXISTE", 0) == 0
               && ScreenCapture::findMonitor (monitors, "\\\\.\\NOEXISTE", 99) == -1,
           "captura: pantalla por nombre, si no por numero", (double) ScreenCapture::findMonitor (monitors, monitors.back().id, 0),
           (double) monitors.size() - 1);

    Compositor compositor ({ 1280, 720, 30, true });
    if (! compositor.isReady())
        return;

    // El compositor: una fuente de la GPU sin alfa se ve opaca y su puntero se coloca encima
    // (100x100 encajada en 1280x720 = 720x720 desde x = 280: el puntero en (50, 50) va a (640, 360)).
    {
        auto fake = std::make_shared<FakeGpuSource>();
        Compositor::Layer layer;
        layer.gpu = fake;
        Compositor::Layer again = layer;   // la misma fuente dos veces: se pone al dia una sola vez
        compositor.renderOnce ({ layer, again });
        auto img = compositor.readProgram();
        const auto blue = img.getPixelAt (400, 200), cur = img.getPixelAt (676, 396), outside = img.getPixelAt (630, 396);
        check (blue.getBlue() > 250 && blue.getRed() < 5 && cur.getRed() > 250 && cur.getBlue() > 250 && outside.getRed() < 5
                   && fake->updates == 1,
               "captura: imagen de la GPU opaca, puntero encima, una sola puesta al dia", cur.getRed(), 255.0);
        // Recortada a la mitad izquierda (u de 0 a 0.5): el puntero en u = 0.5..0.6 queda fuera.
        layer.uv = { 0.0f, 0.0f, 0.5f, 1.0f };
        layer.area = { 0.0f, 0.0f, 640.0f, 720.0f };
        compositor.renderOnce ({ layer });
        img = compositor.readProgram();
        const auto edge = img.getPixelAt (636, 396);
        check (edge.getBlue() > 250 && edge.getRed() < 5, "captura: el puntero se recorta con la imagen", edge.getRed(), 0.0);
    }

    // Captura de pantalla (DXGI): la imagen tiene el tamano de la pantalla y no es todo negro.
    {
        SceneItem screen;
        screen.id = 1;
        screen.type = SourceType::screen;
        screen.monitorId = monitors[0].id;
        auto capture = std::make_shared<Capture> (compositor.getDevice(), screen);
        Compositor::Layer layer;
        layer.gpu = capture;
        juce::Image img;
        const auto until = juce::Time::getMillisecondCounter() + 3000;
        bool any = false;
        while (! any && juce::Time::getMillisecondCounter() < until)
        {
            compositor.renderOnce ({ layer });
            img = compositor.readProgram();
            for (int y = 0; y < 720 && ! any; y += 9)
                for (int x = 0; x < 1280 && ! any; x += 9)
                    any = img.getPixelAt (x, y).getBrightness() > 0.05f;
            if (! any)
                juce::Thread::sleep (30);
        }
        const auto size = capture->getSize();
        std::printf ("        (pantalla %s: %dx%d, %s)\n", monitors[0].id.toRawUTF8(), size.x, size.y,
                     capture->getStatus().isEmpty() ? capture->getMethod().toRawUTF8() : capture->getStatus().toRawUTF8());
        check (size == juce::Point<int> (monitors[0].bounds.getWidth(), monitors[0].bounds.getHeight()) && any,
               "captura: pantalla del tamano del monitor y con imagen", size.x, monitors[0].bounds.getWidth());
    }

    // Captura de ventana (Windows Graphics Capture): una ventana roja de prueba de 320x240.
    if (! ScreenCapture::windowCaptureSupported())
    {
        std::printf ("        (este Windows no captura ventanas: se salta)\n");
        return;
    }
    WNDCLASSW wc {};
    wc.lpfnWndProc = testWindowProc;
    wc.hInstance = GetModuleHandleW (nullptr);
    wc.hbrBackground = CreateSolidBrush (RGB (255, 0, 0));
    wc.lpszClassName = L"MiDJCaptureTest";
    RegisterClassW (&wc);
    RECT rc { 0, 0, 320, 240 };
    AdjustWindowRect (&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW (L"MiDJCaptureTest", L"MiDJ prueba de captura", WS_OVERLAPPEDWINDOW, 60, 60, rc.right - rc.left,
                               rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow (hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow (hwnd);
    pumpMessages();

    bool listed = false;
    juce::String exe;
    for (const auto& w : ScreenCapture::windows())
        if (w.hwnd == hwnd)
        {
            listed = true;
            exe = w.exe;
        }
    check (listed && exe.endsWithIgnoreCase (".exe"), "captura: la ventana sale en la lista con su programa", listed ? 1.0 : 0.0, 1.0);
    check (ScreenCapture::findWindow ("MiDJ prueba de captura", "MiDJCaptureTest", exe) == hwnd
               && ScreenCapture::findWindow ("MiDJ prueba de captura", "JUCE_otraClase", exe) == hwnd
               && ScreenCapture::findWindow ("Otro titulo", "MiDJCaptureTest", exe) == hwnd
               && ScreenCapture::findWindow ("Otro titulo", "OtraClase", "noexiste.exe") == nullptr,
           "captura: ventana por titulo, clase y programa (o sin el titulo)", 1.0, 1.0);

    {
        CaptureManager manager (compositor.getDevice());
        SceneItem win;
        win.id = 7;
        win.type = SourceType::window;
        win.windowTitle = "MiDJ prueba de captura";
        win.windowClass = "MiDJCaptureTest";
        win.windowExe = exe;
        // Creada en un hilo STA (como el de la interfaz) y usada desde este (MTA, como el compositor).
        std::shared_ptr<Capture> capture;
        std::thread ([&]
        {
            CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
            capture = manager.get (win);
            CoUninitialize();
        }).join();
        SceneItem twin = win;
        twin.id = 8;
        check (manager.get (win) == capture && manager.get (twin) == capture,
               "captura: la misma mientras capture lo mismo (tambien en otro elemento)", 1.0, 1.0);
        // Como en la app: el compositor la usa desde su hilo (MTA) a 30 fps.
        std::mutex sourceLock;
        std::shared_ptr<Compositor::GpuSource> shown = capture;
        compositor.buildFrame = [&] (Compositor::Frame& frame)
        {
            const std::lock_guard<std::mutex> sl (sourceLock);
            Compositor::Layer layer;
            layer.gpu = shown;
            frame.layers.push_back (layer);
        };
        compositor.start();
        auto waitFor = [] (std::function<bool()> done)
        {
            const auto until = juce::Time::getMillisecondCounter() + 3000;
            while (! done() && juce::Time::getMillisecondCounter() < until)
            {
                pumpMessages();
                juce::Thread::sleep (20);
            }
            for (int i = 0; i < 10; ++i)   // unos fotogramas mas, por si algo falla despues
            {
                pumpMessages();
                juce::Thread::sleep (20);
            }
        };
        auto isRed = [&compositor]
        {
            const auto c = compositor.readProgram().getPixelAt (640, 360);
            return c.getRed() > 250 && c.getGreen() < 5;
        };
        waitFor ([&] { return isRed() && capture->getSize() == juce::Point<int> (320, 240); });
        const auto size = capture->getSize();
        check (isRed() && size == juce::Point<int> (320, 240) && capture->getStatus().isEmpty(),
               ("captura: ventana roja, solo el contenido (320x240) " + capture->getStatus()).toRawUTF8(), size.x, 320.0);

        // La ventana cambia de tamano: el grupo de fotogramas se rehace y la imagen sigue.
        RECT bigger { 0, 0, 400, 300 };
        AdjustWindowRect (&bigger, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos (hwnd, nullptr, 0, 0, bigger.right - bigger.left, bigger.bottom - bigger.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        waitFor ([&] { return capture->getSize() == juce::Point<int> (400, 300); });
        check (capture->getSize() == juce::Point<int> (400, 300) && capture->getStatus().isEmpty() && isRed(),
               ("captura: la ventana cambia de tamano (creada en STA) " + capture->getStatus()).toRawUTF8(), capture->getSize().x, 400.0);

        // Sin "solo el contenido", la imagen trae la barra de titulo: es mas alta.
        win.clientArea = false;
        auto whole = manager.get (win);
        {
            const std::lock_guard<std::mutex> sl (sourceLock);
            shown = whole;
        }
        waitFor ([&] { return whole->getSize().y > 300; });
        check (whole != capture && whole->getSize().y > 300 && whole->getStatus().isEmpty(), "captura: con bordes, mas alta (barra de titulo)",
               whole->getSize().y, 330.0);
        compositor.stop();
        compositor.buildFrame = nullptr;

        // Se cierra la ventana: la captura lo dice y no se puede rehacer.
        DestroyWindow (hwnd);
        pumpMessages();
        const bool again = whole->reconnect();
        check (! again && whole->getStatus() == "La ventana no esta abierta", ("captura: ventana cerrada: " + whole->getStatus()).toRawUTF8(),
               again ? 1.0 : 0.0, 0.0);
        manager.keepOnly ({});
        check (manager.find (7) == nullptr && manager.find (8) == nullptr, "captura: se suelta la de un elemento borrado", 0.0, 0.0);
    }
    UnregisterClassW (L"MiDJCaptureTest", wc.hInstance);
    DeleteObject (wc.hbrBackground);
}


// Visualizador: la toma del master, el analisis y el dibujo.
void testAuxAudio()
{
    using namespace midj;
    // Entrada de audio a 44,1 kHz sumada a una salida de 48 kHz: tras llenar la cola, sale
    // seguida (sin huecos) aunque su reloj vaya un poco mas lento o mas rapido.
    for (int extra : { 0, -1, 1 })   // muestras de mas o de menos por cada 10 ms de entrada (~0,23 %)
    {
        AudioInputSource input;
        AuxBus bus;
        bus.prepare (48000.0, 480);
        bus.add (&input);
        std::vector<float> in (441 + 1), out (480);
        double phase = 0.0;
        int gaps = 0;
        double sum = 0.0;
        int counted = 0;
        for (int block = 0; block < 600; ++block)   // 6 s
        {
            const int n = 441 + extra;
            for (int i = 0; i < n; ++i)
            {
                in[(size_t) i] = 0.5f * (float) std::sin (phase);
                phase += 2.0 * juce::MathConstants<double>::pi * 440.0 / 44100.0;
            }
            input.pushForTest (in.data(), in.data(), n, 44100.0);
            std::fill (out.begin(), out.end(), 0.0f);
            bus.addTo (out.data(), nullptr, 480);
            if (block >= 100)
            {
                float peak = 0.0f;
                for (auto v : out)
                {
                    peak = juce::jmax (peak, std::abs (v));
                    sum += (double) v * v;
                }
                counted += 480;
                if (peak < 0.1f)
                    ++gaps;
            }
        }
        const double rms = std::sqrt (sum / juce::jmax (1, counted));
        const juce::String name = "audio auxiliar: entrada de 44,1 kHz en salida de 48 kHz sin huecos"
                                  + juce::String (extra == 0 ? "" : extra < 0 ? " (reloj lento)" : " (reloj rapido)");
        check (gaps == 0 && std::abs (rms - 0.5 / std::sqrt (2.0)) < 0.02, name.toRawUTF8(), (double) gaps, 0.0);
        bus.remove (&input);
    }

    // La capturadora: su audio por nombre (el mismo o el tipico de las capturadoras USB/HDMI).
    const juce::StringArray inputs { "Microfono (Realtek High Definition Audio)", "Digital Audio Interface (USB3. 0 capture)" };
    check (CaptureDeviceSource::guessAudioInput ("USB3. 0 capture", inputs) == inputs[1]
               && CaptureDeviceSource::guessAudioInput ("Elgato HD60 S", inputs) == inputs[1]
               && CaptureDeviceSource::guessAudioInput ("HP Truevision HD", { "Microfono (Realtek High Definition Audio)" }).isEmpty(),
           "capturadora: elige su entrada de audio", 0.0, 0.0);
}

void testMediaSource()
{
    using namespace midj;
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsMedia.mp4");
    if (! writeTestVideo (file, 5))
    {
        check (false, "multimedia: se crea un MP4 de prueba", 0.0, 1.0);
        return;
    }
    juce::AudioFormatManager formats;
    formats.registerFormat (new MediaFoundationAudioFormat(), true);
    AuxBus bus;
    bus.prepare (48000.0, 512);
    MediaSourceManager manager (bus, formats, 640);

    SceneCollection c;
    auto item = c.makeItem (SourceType::media, 1280, 720);
    item.file = file;
    auto source = std::dynamic_pointer_cast<MediaFileSource> (manager.get (item));
    manager.setViewing (true);
    check (source != nullptr && ! source->isPlaying(), "multimedia: con 'reiniciar al activarse' espera a salir al aire", 0.0, 0.0);
    manager.setOnAir ({ item.id });

    // Se oye en tiempo real (como lo pediria la tarjeta) y la imagen va con el sonido.
    juce::AudioBuffer<float> out (2, 512);
    auto playFor = [&] (double seconds)
    {
        double sum = 0.0;
        int n = 0;
        for (int block = 0; block < (int) (seconds * 48000.0 / 512.0); ++block)
        {
            out.clear();
            bus.addTo (out.getWritePointer (0), out.getWritePointer (1), 512);
            if (block > 10)
            {
                sum += out.getRMSLevel (0, 0, 512) * out.getRMSLevel (0, 0, 512);
                ++n;
            }
            juce::Thread::sleep (10);
            manager.tick();
        }
        return std::sqrt (sum / juce::jmax (1, n));
    };
    const double rms = playFor (1.5);
    check (source->hasAudio() && rms > 0.15, "multimedia: su sonido va al master", rms, 0.26);
    const double pos = source->getPosition();
    check (std::abs (pos - 1.5) < 0.25, "multimedia: avanza con el audio pedido", pos, 1.5);
    juce::Thread::sleep (150);
    auto green = [&]
    {
        const auto frame = source->getFrame();
        if (! frame.isValid())
            return juce::Colours::transparentBlack;
        return frame.getPixelAt (frame.getWidth() / 2, frame.getHeight() / 2);
    };
    const auto px = green();
    check (px.getGreen() > 200 && px.getRed() < 60 && px.getBlue() < 60, "multimedia: la imagen es la del segundo 1 (verde)", px.getGreen(), 255.0);

    // Velocidad 200 %: el doble de deprisa.
    item.speedPercent = 200;
    manager.get (item);
    const double before = source->getPosition();
    playFor (0.5);
    const double advanced = source->getPosition() - before;
    check (std::abs (advanced - 1.0) < 0.2, "multimedia: velocidad 200 %", advanced, 1.0);

    // Fuera del aire se para y vuelve al principio sin mostrar nada (reiniciar al activarse).
    manager.setOnAir ({});
    juce::Thread::sleep (100);
    check (! source->isPlaying() && source->getPosition() < 0.05 && ! source->getFrame().isValid(),
           "multimedia: al salir del aire se para y se oculta", source->getPosition(), 0.0);

    // Al aire otra vez: empieza de cero; al terminar (sin bucle) no muestra nada.
    manager.setOnAir ({ item.id });
    playFor (2.8);   // 5,6 s del video a 200 %
    juce::Thread::sleep (100);
    check (! source->isPlaying() && ! source->getFrame().isValid(), "multimedia: al terminar no muestra nada", source->isPlaying() ? 1.0 : 0.0, 0.0);

    // Con bucle sigue sonando desde el principio.
    item.loop = true;
    manager.get (item);
    source->restart();
    playFor (2.8);
    check (source->isPlaying() && source->getPosition() < 1.5, "multimedia: en bucle vuelve a empezar", source->getPosition(), 0.6);

    // Dos elementos con el mismo archivo comparten la fuente; borrar el elemento la suelta.
    auto copy = item;
    copy.id = c.newId();
    check (manager.get (copy) == source, "multimedia: el mismo archivo en otra escena es la misma fuente", 0.0, 0.0);
    manager.keepOnly ({});
    std::weak_ptr<MediaFileSource> weak = source;
    source.reset();
    check (weak.expired(), "multimedia: se suelta al quitar sus elementos", 0.0, 0.0);
    file.deleteFile();

    // Escenas: los ajustes de multimedia y capturadora se guardan.
    auto dev = c.makeItem (SourceType::captureDevice, 1280, 720);
    dev.deviceId = "\\\\?\\usb#vid_534d";
    dev.deviceName = "USB3. 0 capture";
    dev.customFormat = true;
    dev.captureWidth = 1920;
    dev.captureHeight = 1080;
    dev.captureFps = 60.0;
    dev.captureFormat = "MJPG";
    dev.audioDevice = "Digital Audio Interface (USB3. 0 capture)";
    dev.volume = 0.5f;
    auto& scene = c.addScene ("Captura");
    item.matte = true;
    scene.items.push_back (item);
    scene.items.push_back (dev);
    const auto back = SceneCollection::fromXml (*c.toXml(), 1280, 720);
    const auto* s = back.findScene (scene.id);
    check (s != nullptr && s->items.size() == 2 && s->items[0].type == SourceType::media && s->items[0].loop && s->items[0].speedPercent == 200
               && s->items[0].matte && ! s->items[1].matte
               && s->items[0].file == item.file && s->items[1].type == SourceType::captureDevice && s->items[1].deviceId == dev.deviceId
               && s->items[1].captureFps == 60.0 && s->items[1].captureFormat == "MJPG" && s->items[1].audioDevice == dev.audioDevice
               && s->items[1].volume == 0.5f,
           "escenas: multimedia y capturadora se guardan en XML", 0.0, 0.0);

    // Dispositivos de captura de este equipo (solo informa: depende de lo que este conectado).
    const auto devices = VideoCaptureReader::devices();
    std::printf ("  (dispositivos de captura de video: %d)\n", (int) devices.size());
    for (const auto& d : devices)
    {
        const auto fmts = VideoCaptureReader::formats (d.id);
        juce::String error;
        auto reader = VideoCaptureReader::open (d.id, {}, 640, false, error);
        juce::Image frame;
        const bool got = reader != nullptr && reader->read (frame, false);
        std::printf ("  - %s: %d formatos, predeterminado %dx%d %s a %.0f fps, fotograma %s\n", d.name.toRawUTF8(), (int) fmts.size(),
                     reader != nullptr ? reader->getFormat().width : 0, reader != nullptr ? reader->getFormat().height : 0,
                     reader != nullptr ? reader->getFormat().format.toRawUTF8() : error.toRawUTF8(), reader != nullptr ? reader->getFormat().fps : 0.0,
                     got ? (juce::String (frame.getWidth()) + "x" + juce::String (frame.getHeight())).toRawUTF8() : "no");
    }
}

void testGpuFrames()
{
    using namespace midj;
    // Con el dispositivo del compositor compartido, el video se decodifica en la GPU y el
    // fotograma llega al programa sin pasar por la CPU (textura a textura).
    Compositor compositor ({ 1280, 720, 30, true });
    if (! compositor.isReady() || ! compositor.hasVideoSupport())
    {
        std::printf ("  (sin GPU con video: se salta la prueba de fotogramas en la GPU)\n");
        return;
    }
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsGpu.mp4");
    writeTestVideo (file, 3);
    MediaFoundation::shareDevice (compositor.getDevice());
    juce::String error;
    auto decoder = VideoDecoder::open (file, 1280, error, true);
    check (decoder != nullptr && decoder->isOnSharedDevice(), "gpu: el video se decodifica en el dispositivo del compositor", 0.0, 0.0);
    if (decoder != nullptr && decoder->isOnSharedDevice())
    {
        std::shared_ptr<GpuVideoFrame> frame;
        const bool got = decoder->frameAt (1.5, frame);   // segundo 1: verde
        auto source = std::make_shared<GpuFrameSource>();
        source->publish (frame);
        frame.reset();
        Compositor::Layer layer;
        layer.gpu = source;
        compositor.renderOnce ({ layer });
        const auto program = compositor.readProgram();
        const auto px = program.isValid() ? program.getPixelAt (640, 360) : juce::Colour();
        check (got && px.getGreen() > 200 && px.getRed() < 60 && px.getBlue() < 60, "gpu: el fotograma llega al programa sin pasar por la CPU",
               px.getGreen(), 255.0);

        // Tras ocultarlo no se ve nada.
        source->clear();
        compositor.renderOnce ({ layer });
        const auto after = compositor.readProgram();
        check (after.isValid() && after.getPixelAt (640, 360).getBrightness() < 0.05f, "gpu: oculto, no se dibuja", 0.0, 0.0);

        // Coste por fotograma: decodificar en la GPU sin copiar a la CPU frente a copiando.
        std::shared_ptr<GpuVideoFrame> g;
        juce::Image cpu;
        auto cpuDecoder = VideoDecoder::open (file, 1280, error, true);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 60; ++i)
            decoder->frameAt (i / 30.0, g);
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        MediaFoundation::shareDevice (nullptr);
        auto ownDecoder = VideoDecoder::open (file, 1280, error, true);   // su propio dispositivo: copia a la CPU
        const auto t2 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; ownDecoder != nullptr && i < 60; ++i)
            ownDecoder->frameAt (i / 30.0, cpu);
        const auto t3 = juce::Time::getMillisecondCounterHiRes();
        std::printf ("  (320x240: %.2f ms por fotograma en la GPU, %.2f ms copiando a la CPU)\n", (t1 - t0) / 60.0, (t3 - t2) / 60.0);
    }
    MediaFoundation::shareDevice (nullptr);
    decoder.reset();
    file.deleteFile();
}

void testVisualizer()
{
    using namespace midj;
    // Toma del master: lo ultimo escrito, menos la latencia de salida; antes de empezar, silencio.
    MasterTap tap;
    tap.prepare (44100.0);
    std::vector<float> got (10, -1.0f);
    tap.read (got.data(), 10, 0.0);
    check (got[0] == 0.0f && got[9] == 0.0f, "visualizador: sin audio, silencio", got[9], 0.0);
    std::vector<float> ramp (1000);
    for (int i = 0; i < 1000; ++i)
        ramp[(size_t) i] = (float) i;
    tap.push (ramp.data(), ramp.data(), 1000);
    tap.read (got.data(), 10, 0.0);
    check (got[0] == 990.0f && got[9] == 999.0f, "visualizador: lee las ultimas muestras del master", got[9], 999.0);
    tap.setOutputLatency (100);
    tap.read (got.data(), 10, 0.0);
    check (got[9] == 899.0f, "visualizador: muestra lo que se oye (menos la latencia)", got[9], 899.0);

    tap.setOutputLatency (0);
    tap.setBeat (true, 8.0, 120.0);
    auto beat = tap.beatAt (juce::Time::getMillisecondCounterHiRes() + 500.0);   // medio segundo despues = un pulso a 120
    check (beat.valid && std::abs (beat.beat - 9.0) < 0.05, "visualizador: el pulso sigue corriendo entre bloques", beat.beat, 9.0);
    check (Visualizer::beatFlash (4.0) > 0.99f && std::abs (Visualizer::beatFlash (5.0) - 0.75f) < 0.01f && Visualizer::beatFlash (4.5) < 0.06f,
           "visualizador: destello en el pulso (mas en el primero del compas)", Visualizer::beatFlash (4.5), 0.05);

    // Analisis: un seno de 1 kHz a -6 dB sube su barra a 0,9 y deja las lejanas abajo.
    Visualizer vis (tap);
    std::vector<float> sine (Visualizer::fftSize);
    auto makeSine = [&] (float hz, float amp)
    {
        for (int i = 0; i < Visualizer::fftSize; ++i)
            sine[(size_t) i] = amp * std::sin (juce::MathConstants<float>::twoPi * hz * (float) i / 44100.0f);
    };
    auto barOf = [] (float hz)
    {
        for (int b = 0; b < Visualizer::numBars; ++b)
            if (hz >= Visualizer::barEdge (b) && hz < Visualizer::barEdge (b + 1))
                return b;
        return 0;
    };
    std::array<float, Visualizer::numBars> bars {};
    makeSine (1000.0f, 0.5f);
    vis.analyse (sine.data(), 44100.0, bars);
    const int b1k = barOf (1000.0f);
    check (std::abs (bars[(size_t) b1k] - 0.9f) < 0.05f, "visualizador: 1 kHz a -6 dB = barra al 90 %", bars[(size_t) b1k], 0.9);
    check (bars[(size_t) barOf (100.0f)] < 0.2f && bars[(size_t) barOf (8000.0f)] < 0.2f, "visualizador: las barras lejanas se quedan abajo",
           bars[(size_t) barOf (100.0f)], 0.0);
    const float lv = Visualizer::level (sine.data(), 1024);
    check (std::abs (lv - 0.82f) < 0.02f && Visualizer::level (std::vector<float> (1024).data(), 1024) == 0.0f,
           "visualizador: volumen (seno a -6 dB = -9 dB RMS)", lv, 0.82);
    const int t = Visualizer::triggerPoint (sine.data(), Visualizer::fftSize, Visualizer::waveSamples);
    check (t > 0 && sine[(size_t) t - 1] < 0.0f && sine[(size_t) t] >= 0.0f && t <= Visualizer::fftSize - Visualizer::waveSamples,
           "visualizador: la onda empieza en un cruce por cero hacia arriba", t, 0.0);
    makeSine (60.0f, 0.5f);
    vis.analyse (sine.data(), 44100.0, bars);
    const auto loudest = (int) (std::max_element (bars.begin(), bars.end()) - bars.begin());
    check (std::abs (loudest - barOf (60.0f)) <= 1, "visualizador: un grave de 60 Hz en su barra", loudest, barOf (60.0f));
    check (Visualizer::renderSize (1280, 240) == juce::Point<int> (640, 120) && Visualizer::renderSize (300, 300) == juce::Point<int> (300, 300),
           "visualizador: se dibuja en pequeno (como mucho 640 px)", Visualizer::renderSize (1280, 240).x, 640.0);

    // Dibujo: en silencio no hay nada (la onda, una linea); con un seno, la barra de su frecuencia.
    MasterTap quiet;
    quiet.prepare (44100.0);
    Visualizer silent (quiet);
    SceneCollection c;
    auto item = c.makeItem (SourceType::visualizer, 1280, 720);
    auto countOpaque = [] (const juce::Image& img, int x0, int x1)
    {
        x1 = juce::jmin (x1, img.getWidth());
        int n = 0;
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = x0; x < x1; ++x)
                n += img.getPixelAt (x, y).getAlpha() > 128 ? 1 : 0;
        return n;
    };
    double ms = 1000.0;
    item.visualizer = VisualizerStyle::spectrum;
    int n = countOpaque (silent.render (item, ms += 33.0), 0, 640);
    check (n == 0, "visualizador: espectro en silencio vacio", n, 0.0);
    item.visualizer = VisualizerStyle::pulse;
    n = countOpaque (silent.render (item, ms += 33.0), 0, 640);
    check (n == 0, "visualizador: pulso en silencio vacio", n, 0.0);
    item.visualizer = VisualizerStyle::wave;
    const auto& flat = silent.render (item, ms += 33.0);
    check (flat.getPixelAt (320, 60).getAlpha() > 100 && flat.getPixelAt (320, 2).getAlpha() == 0,
           "visualizador: onda en silencio = linea en el centro", flat.getPixelAt (320, 60).getAlpha(), 255.0);

    MasterTap loud;
    loud.prepare (44100.0);
    makeSine (1000.0f, 0.5f);
    loud.push (sine.data(), sine.data(), Visualizer::fftSize);
    Visualizer drawing (loud);
    item.visualizer = VisualizerStyle::spectrum;
    const auto& img = drawing.render (item, 1000.0);
    const float slot = (float) img.getWidth() / Visualizer::numBars;
    const int inBar = countOpaque (img, (int) (slot * (float) b1k + slot * 0.4f), (int) (slot * (float) b1k + slot * 0.6f));
    const int farBar = countOpaque (img, (int) (slot * 2.4f), (int) (slot * 2.6f));
    check (img.getWidth() == 640 && img.getHeight() == 120 && inBar > 0 && farBar == 0, "visualizador: espectro dibujado (la barra de 1 kHz arriba)",
           inBar, 1.0);
    item.visualizer = VisualizerStyle::pulse;
    n = countOpaque (drawing.render (item, 1033.0), 0, 640);
    check (n > 0, "visualizador: pulso con sonido = circulo", n, 1.0);

    // Cuanto cuesta dibujar cada estilo (solo informa: en Debug es mucho mas lento).
    for (auto style : { VisualizerStyle::spectrum, VisualizerStyle::wave, VisualizerStyle::pulse })
    {
        item.visualizer = style;
        item.width = style == VisualizerStyle::pulse ? 360 : 1280;
        item.height = style == VisualizerStyle::pulse ? 360 : 240;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int f = 0; f < 200; ++f)
            drawing.render (item, 2000.0 + f * 33.0);
        std::printf ("        visualizador %s: %.2f ms por fotograma\n", SceneCollection::visualizerName (style).toRawUTF8(),
                     (juce::Time::getMillisecondCounterHiRes() - t0) / 200.0);
    }

    // Escenas: estilo, color y tamano en XML.
    auto& scene = c.addScene ("Vis");
    item.visualizer = VisualizerStyle::wave;
    item.colour = juce::Colours::orange;
    item.width = 500;
    scene.items.push_back (item);
    const auto back = SceneCollection::fromXml (*c.toXml(), 1280, 720);
    const auto& r = back.findScene (scene.id)->items[0];
    check (r.type == SourceType::visualizer && r.visualizer == VisualizerStyle::wave && r.colour == juce::Colours::orange && r.width == 500,
           "visualizador: se guarda en las escenas", r.width, 500.0);

    // Del mezclador: un deck sonando a 120 BPM llega a la toma con su pulso.
    juce::AudioFormatManager formats;
    Deck a (formats), b (formats);
    Mixer mixer (a, b);
    juce::AudioBuffer<float> audio (2, 4 * 44100);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < audio.getNumSamples(); ++i)
            audio.setSample (ch, i, 0.25f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / 44100.0f));
    a.loadBuffer (audio, 44100.0, "seno", { 120.0, 0.0 });
    mixer.prepareToPlay (512, 44100.0);
    a.play();
    juce::AudioBuffer<float> out (2, 512);
    for (int block = 0; block < 86; ++block)   // ~1 s
    {
        out.clear();
        juce::AudioSourceChannelInfo info (&out, 0, 512);
        mixer.getNextAudioBlock (info);
    }
    beat = mixer.getTap().beatAt (0.0);
    std::vector<float> heard (1024);
    mixer.getTap().read (heard.data(), 1024, 0.0);
    const double expected = 86.0 * 512.0 / 22050.0;
    check (beat.valid && beat.bpm == 120.0 && std::abs (beat.beat - expected) < 0.05 && Visualizer::level (heard.data(), 1024) > 0.5f,
           "visualizador: el mezclador da el audio y el pulso del deck que suena", beat.beat, expected);
}

void testVideoMix()
{
    using midj::VideoMix;
    auto w = VideoMix::weights (0.25f, true, true);
    check (w[0] == 0.75f && w[1] == 0.25f, "mezcla de video: pesos del crossfader", w[1], 0.25);
    w = VideoMix::weights (0.0f, false, true);
    check (w[0] == 0.0f && w[1] == 1.0f, "mezcla de video: deck vacio = solo el otro", w[1], 1.0);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    midj::Deck a (formats), b (formats);
    midj::Mixer mixer (a, b);
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsMix.mp4");
    writeTestVideo (file, 5);
    a.loadBuffer (juce::AudioBuffer<float> (2, 5 * 44100), 44100.0, "video", { 120.0, 0.0 });
    a.setPositionSeconds (1.5);   // verde
    midj::DeckVideo videoA (a, 160), videoB (b, 160);
    VideoMix mix ({ &videoA, &videoB }, { &a, &b }, mixer);
    mix.setProgramSize (320, 180);

    std::vector<midj::Compositor::Layer> layers;
    mix.buildLayers (layers);
    check (layers.size() == 1 && layers[0].slot == 4, "mezcla de video: sin nada, el logo", (double) layers.size(), 1.0);
    // El logo: el disco con su etiqueta naranja (al lado del agujero del centro) sobre negro.
    const auto logo = VideoMix::renderLogo (640, 360);
    const auto label = logo.getPixelAt (326, (int) ((360.0f - 360.0f * 0.33f) * 0.5f + 360.0f * 0.1f));
    check (label.getRed() > 90 && label.getRed() > 2 * label.getGreen() && logo.getPixelAt (4, 4).getBrightness() < 0.01f,
           "mezcla de video: el logo es el disco naranja sobre negro", label.getRed(), 115.0);

    videoA.setFile (file);
    videoA.setActive (true);
    for (int i = 0; i < 300 && videoA.getFrameTime() < 1.4; ++i)   // tras alcanzar a la posicion del deck
        juce::Thread::sleep (10);
    mix.setTitle (1, "Artista - Cancion");
    mixer.setCrossfader (0.5f);
    layers.clear();
    mix.buildLayers (layers);
    check (layers.size() == 2 && layers[0].slot == 0 && layers[1].slot == 3 && std::abs (layers[1].opacity - 0.5f) < 1e-6f,
           "mezcla de video: video A debajo y rotulo de B encima al 50 %", (double) layers.size(), 2.0);
    mixer.setCrossfader (0.0f);
    layers.clear();
    mix.buildLayers (layers);
    check (layers.size() == 1 && layers[0].slot == 0 && dominantColour (layers[0].image) == 'G',
           "mezcla de video: crossfader a la izquierda = solo el video de A", (double) layers.size(), 1.0);

    // De la mezcla al programa en la GPU: el video verde de A.
    midj::Compositor compositor ({ 320, 180, 30, true });
    if (compositor.isReady())
    {
        compositor.renderOnce (layers);
        check (dominantColour (compositor.readProgram()) == 'G', "mezcla de video: el programa compuesto es el video de A", 0.0, 0.0);
    }
    videoA.setFile ({});
    juce::Thread::sleep (50);
    file.deleteFile();
}

// Tiempo de CPU del proceso en segundos (todos sus hilos: Media Foundation decodifica con varios).
double processCpuSeconds()
{
    FILETIME created, exited, kernel, user;
    GetProcessTimes (GetCurrentProcess(), &created, &exited, &kernel, &user);
    auto toSeconds = [] (const FILETIME& t) { return (double) ((ULONGLONG) t.dwHighDateTime << 32 | t.dwLowDateTime) / 1.0e7; };
    return toSeconds (kernel) + toSeconds (user);
}

// MiDJTests --video archivo: datos del video y coste de decodificarlo en la GPU y en la CPU.
int videoInfo (const juce::File& file)
{
    juce::Image image;
    for (const bool gpu : { true, false })
    {
        juce::String error;
        auto video = midj::VideoDecoder::open (file, 960, error, gpu);
        if (video == nullptr)
        {
            std::printf ("%s\n", error.toRawUTF8());
            return 1;
        }
        std::printf ("%s [%s]: %dx%d (reducido), %.2f fps, %.1f s\n", file.getFileName().toRawUTF8(),
                     video->isUsingGpu() ? "GPU" : "CPU", video->getWidth(), video->getHeight(), video->getFrameRate(), video->getDuration());
        const auto start = juce::Time::getMillisecondCounterHiRes();
        const double cpuStart = processCpuSeconds();
        int frames = 0;
        for (double t = 10.0; t < 20.0; t += 1.0 / video->getFrameRate(), ++frames)
            if (! video->frameAt (t, image))
                break;
        const double took = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
        const double cpu = processCpuSeconds() - cpuStart;
        // Coste al reproducir: CPU por cada segundo de video, en % de un nucleo y del procesador entero.
        const int cores = juce::SystemStats::getNumCpus();
        std::printf ("  10 s de video (%d fotogramas) en %.2f s; CPU %.2f s = %.0f %% de un nucleo al reproducir (%.0f %% de %d nucleos)\n",
                     frames, took, cpu, cpu / 10.0 * 100.0, cpu / 10.0 * 100.0 / cores, cores);
        // Lo que hace un deck tras un salto: entregar enseguida un fotograma y seguir hasta alcanzar.
        const auto jumpStart = juce::Time::getMillisecondCounterHiRes();
        video->frameAt (100.0, image, 1);
        const double firstFrame = juce::Time::getMillisecondCounterHiRes() - jumpStart;
        const double keyFrame = video->getCurrentFrameTime();
        while (video->isCatchingUp() && video->frameAt (100.0, image, 6)) {}
        std::printf ("  salto a 100 s: primer fotograma (el clave de %.2f s) en %.0f ms, alcanzado en %.0f ms\n",
                     keyFrame, firstFrame, juce::Time::getMillisecondCounterHiRes() - jumpStart);
        video->frameAt (30.0, image);
        // El fotograma de los 30 s, para mirarlo.
        const auto png = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile (juce::String ("MiDJ video 30s ") + (video->isUsingGpu() ? "GPU" : "CPU") + ".png");
        png.deleteFile();
        juce::FileOutputStream out (png);
        if (image.isValid() && juce::PNGImageFormat().writeImageToStream (image, out))
            std::printf ("  fotograma de los 30 s: %s\n", png.getFullPathName().toRawUTF8());
    }
    return 0;
}

// --- Biblioteca: favoritos, mas reproducidos y listas propias -----------------------------------

void testLibraryLists()
{
    std::printf ("== Biblioteca: favoritos, mas reproducidos y listas ==\n");
    using midj::Library;
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsLists");
    root.deleteRecursively();
    const auto music = root.getChildFile ("Musica");
    const auto a = music.getChildFile ("Artista A - Uno.wav"), b = music.getChildFile ("Artista B - Dos.wav"),
               c = music.getChildFile ("Artista C - Tres.wav");
    for (const auto& f : { a, b, c })
        writeClickWav (f, 120.0);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const auto dbFile = root.getChildFile ("library.db");
    {
        Library library (formats);
        library.open (dbFile);
        library.setDurationLimits ({});
        library.addFolder (music);
        library.scanNow();

        library.setFavorite (b, true);
        library.setFavorite (c, true);
        library.setFavorite (c, false);
        check (library.countFavorites() == 1 && library.getFavorites()[0].getFile() == b, "favoritos: marcar y desmarcar",
               library.countFavorites(), 1.0);

        library.addPlay (a);
        library.addPlay (c);
        library.addPlay (c);
        const auto most = library.getMostPlayed();
        check (most.size() == 2 && most[0].getFile() == c && most[0].plays == 2, "mas reproducidos: la que mas sono primero",
               most.empty() ? 0.0 : most[0].plays, 2.0);

        const auto list = library.createPlaylist ("Bailables");
        check (list != 0, "listas: crear", (double) list, 1.0);
        library.addToPlaylist (list, c);
        library.addToPlaylist (list, a);
        check (! library.addToPlaylist (list, a), "listas: no repite una cancion", 0.0, 0.0);
        check (! library.addToPlaylist (list, music.getChildFile ("no existe.wav")), "listas: solo canciones de la biblioteca", 0.0, 0.0);
        auto tracks = library.getPlaylistTracks (list);
        check (tracks.size() == 2 && tracks[0].getFile() == c && tracks[1].getFile() == a, "listas: en el orden en que se anadieron",
               (double) tracks.size(), 2.0);
        library.addToPlaylist (list, b);
        library.moveInPlaylist (list, tracks[1].id, 0);   // a al principio
        tracks = library.getPlaylistTracks (list);
        check (tracks.size() == 3 && tracks[0].getFile() == a && tracks[1].getFile() == c && tracks[2].getFile() == b,
               "listas: mover al principio", (double) tracks.size(), 3.0);
        library.removeFromPlaylist (list, tracks[1].id);
        library.renamePlaylist (list, "Fiesta");
        const auto lists = library.getPlaylists();
        check (lists.size() == 1 && lists[0].name == "Fiesta" && lists[0].count == 2, "listas: quitar una y renombrar",
               lists.empty() ? 0.0 : lists[0].count, 2.0);
    }
    {
        Library library (formats);   // reabrir: todo guardado
        library.open (dbFile);
        const auto lists = library.getPlaylists();
        check (library.countFavorites() == 1 && library.countPlayed() == 2 && lists.size() == 1 && lists[0].count == 2,
               "reabrir: favoritos, reproducciones y listas guardados", (double) lists.size(), 1.0);
        library.deletePlaylist (lists[0].id);
        check (library.getPlaylists().empty() && library.getNumTracks() == 3, "listas: borrar la lista no borra canciones",
               library.getNumTracks(), 3.0);
    }
    root.deleteRecursively();
}

// --- YouTube: leer la pagina de resultados, duraciones y enlaces --------------------------------

void testAlphaVideo()
{
    std::printf ("== Videos con transparencia (a mate con FFmpeg) ==\n");
    using midj::AlphaVideo::parseInfo;
    // Lo que dice ffmpeg -i de un WebM VP9 con alfa (el alfa va en el metadato) y de un MOV ProRes 4444.
    const auto webm = parseInfo (R"(Input #0, matroska,webm, from 'logo.webm':
  Duration: 00:00:02.03, start: 0.000000, bitrate: 2522 kb/s
  Stream #0:0: Video: vp9 (Profile 0), yuv420p(tv, bt709, progressive), 1920x1080, SAR 1:1 DAR 16:9, 30 fps, 30 tbr, 1k tbn (default)
    Metadata:
      alpha_mode      : 1
      ENCODER         : Lavc63.23.100 libvpx-vp9)");
    check (webm.alpha && webm.width == 1920 && webm.height == 1080 && std::abs (webm.seconds - 2.03) < 0.001,
           "webm VP9: alfa por alpha_mode, 1920x1080 y 2,03 s", webm.seconds, 2.03);
    const auto mov = parseInfo (R"(Input #0, mov,mp4,m4a,3gp,3g2,mj2, from 'logo.mov':
  Duration: 00:01:02.50, start: 0.000000, bitrate: 183461 kb/s
  Stream #0:0[0x1]: Video: prores (4444) (ap4h / 0x68347061), yuva444p12le(tv, bt709, progressive), 1280x720, 183456 kb/s, 30 fps (default)
  Stream #0:1[0x2]: Audio: aac (LC) (mp4a / 0x6134706D), 48000 Hz, stereo, fltp, 192 kb/s (default))");
    check (mov.alpha && mov.width == 1280 && mov.height == 720 && std::abs (mov.seconds - 62.5) < 0.001,
           "mov ProRes 4444: alfa por yuva, 1280x720 (no el codigo 0x...) y 62,5 s", (double) mov.width, 1280.0);
    const auto plain = parseInfo ("  Duration: 00:00:05.00\n  Stream #0:0: Video: h264 (High), yuv420p(progressive), 1920x1080, 30 fps\n"
                                  "  Stream #0:1: Video: png, rgba(pc), 300x300 (attached pic)");
    check (! plain.alpha && plain.width == 1920, "mp4 normal: sin alfa (la caratula con alfa no cuenta)", (double) plain.alpha, 0.0);
    check (midj::AlphaVideo::mightHaveAlpha (juce::File ("C:\\a\\b.WEBM")) && midj::AlphaVideo::mightHaveAlpha (juce::File ("C:\\a\\b.mov"))
               && ! midj::AlphaVideo::mightHaveAlpha (juce::File ("C:\\a\\b.mp4")),
           "solo .webm, .mov y .mkv", 0.0, 0.0);
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJTestsAlpha");
    folder.deleteRecursively();
    folder.createDirectory();
    check (midj::AlphaVideo::matteFileFor (folder.getChildFile ("logo.webm")) == folder.getChildFile ("logo_mate.mp4"),
           "el convertido: <nombre>_mate.mp4 junto al original", 0.0, 0.0);

    // De verdad (si hay FFmpeg): un MOV con alfa de 1 s, 160x90, a mate en 64x64 como maximo.
    const auto ffmpeg = midj::AlphaVideo::findFfmpeg();
    if (! ffmpeg.existsAsFile())
    {
        std::printf ("  (sin FFmpeg: no se prueba la conversion)\n");
        return;
    }
    const auto source = folder.getChildFile ("prueba.mov");
    juce::ChildProcess make;
    make.start (juce::StringArray { ffmpeg.getFullPathName(), "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                                    "color=c=red@0.5:s=160x90:d=1,format=argb", "-c:v", "qtrle", source.getFullPathName() });
    make.waitForProcessToFinish (60000);
    midj::AlphaVideo::Info info;
    check (midj::AlphaVideo::probe (ffmpeg, source, info) && info.alpha && info.width == 160 && std::abs (info.seconds - 1.0) < 0.1,
           "FFmpeg: el MOV de prueba tiene alfa, 160x90 y 1 s", info.seconds, 1.0);
    const auto output = midj::AlphaVideo::matteFileFor (source);
    double lastProgress = 0.0;
    juce::String error;
    const bool ok = midj::AlphaVideo::convert (ffmpeg, source, output, 64, 64, info.seconds,
                                               [&lastProgress] (double p) { lastProgress = p; return true; }, error);
    midj::AlphaVideo::Info out;
    midj::AlphaVideo::probe (ffmpeg, output, out);
    // 160x90 cabe en 64x64 sin deformar: 64x36; con el mate al lado, 128x36.
    check (ok && output.existsAsFile() && out.width == 128 && out.height == 36 && ! out.alpha,
           "convertido: 128x36 (dos mitades de 64x36), H.264 sin alfa", (double) out.width, 128.0);
    check (! output.getSiblingFile ("prueba_mate.convirtiendo.mp4").exists(), "sin temporal al terminar", 0.0, 0.0);
    std::printf ("  (avance final %.2f%s)\n", lastProgress, error.isEmpty() ? "" : (" " + error).toRawUTF8());
    folder.deleteRecursively();
}

void testYouTubeSearch()
{
    std::printf ("== YouTube: busqueda ==\n");
    using midj::YouTubeSearch::parseDuration;
    using midj::YouTubeSearch::idFromText;
    check (parseDuration ("3:41") == 221.0 && parseDuration ("1:02:03") == 3723.0 && parseDuration ("EN VIVO") == 0.0,
           "duraciones: 3:41, 1:02:03 y en vivo", parseDuration ("3:41"), 221.0);
    check (midj::YouTubeSearch::formatDuration (221.0) == "3:41" && midj::YouTubeSearch::formatDuration (3723.0) == "1:02:03",
           "duraciones: escribirlas", 0.0, 0.0);
    check (idFromText ("https://www.youtube.com/watch?v=pnEHsUWFuNM&list=x") == "pnEHsUWFuNM"
               && idFromText ("https://youtu.be/bJ1Y9S6SR9I?t=10") == "bJ1Y9S6SR9I"
               && idFromText ("https://www.youtube.com/shorts/CCF1_jI8Prk") == "CCF1_jI8Prk"
               && idFromText ("nq9TVmwWoOM") == "nq9TVmwWoOM" && idFromText ("nicky jam").isEmpty(),
           "enlaces: watch, youtu.be, shorts e id; texto normal no", 0.0, 0.0);

    // Una pagina de resultados en miniatura, como la de YouTube: dos videos, un directo (sin
    // duracion, se descarta), un repetido y una lista (no es un video). Arriba un filtro con su
    // propia continuacion (no es la de la lista) y al final la de la lista.
    const juce::String page = R"html(<html><script>ytcfg.set({"INNERTUBE_CLIENT_VERSION":"2.20261023.01.00"});</script>
      <script>var ytInitialData = {"header":{"chipCloudChipRenderer":{"navigationEndpoint":{"continuationCommand":{"token":"FILTRO"}}}},
      "contents":{"twoColumnSearchResultsRenderer":{"primaryContents":
      {"sectionListRenderer":{"contents":[{"itemSectionRenderer":{"contents":[
        {"videoRenderer":{"videoId":"pnEHsUWFuNM","title":{"runs":[{"text":"Piensas En Mi - Nicky Jam"}]},
          "ownerText":{"runs":[{"text":"NickyJamTV"}]},"lengthText":{"simpleText":"4:16"}}},
        {"videoRenderer":{"videoId":"LIVE0000001","title":{"runs":[{"text":"Radio en vivo"}]},"ownerText":{"runs":[{"text":"Radio"}]}}},
        {"playlistRenderer":{"playlistId":"PL123","title":{"simpleText":"Mix"}}},
        {"videoRenderer":{"videoId":"bJ1Y9S6SR9I","title":{"runs":[{"text":"La Mar"},{"text":"ia"}]},
          "longBylineText":{"runs":[{"text":"Luister La Voz"}]},"lengthText":{"simpleText":"3:41"}}},
        {"videoRenderer":{"videoId":"pnEHsUWFuNM","title":{"runs":[{"text":"Repetido"}]},"lengthText":{"simpleText":"4:16"}}}
      ]}},{"continuationItemRenderer":{"trigger":"CONTINUATION_TRIGGER_ON_ITEM_SHOWN","continuationEndpoint":
        {"continuationCommand":{"token":"PAGINA2","request":"CONTINUATION_REQUEST_TYPE_SEARCH"}}}}]}}}}};</script></html>)html";
    const auto results = midj::YouTubeSearch::parseResultsPage (page);
    const auto& videos = results.videos;
    check (videos.size() == 2, "resultados: 2 videos (sin directos, listas ni repetidos)", (double) videos.size(), 2.0);
    check (videos.size() == 2 && videos[0].id == "pnEHsUWFuNM" && videos[0].channel == "NickyJamTV" && videos[0].seconds == 256.0
               && videos[1].title == "La Maria" && videos[1].channel == "Luister La Voz",
           "resultados: id, titulo (en partes), canal y duracion", videos.empty() ? 0.0 : videos[0].seconds, 256.0);
    check (results.continuation == "PAGINA2" && results.clientVersion == "2.20261023.01.00",
           "resultados: continuacion de la lista (no la del filtro) y version de la web", 0.0, 0.0);
    check (midj::YouTubeSearch::parseResultsPage ("<html>sin datos</html>").videos.empty(), "pagina sin datos: nada", 0.0, 0.0);

    // La respuesta de los siguientes (POST youtubei/v1/search): los videos y la continuacion siguiente.
    const auto more = midj::YouTubeSearch::parseMoreResults (R"json({"onResponseReceivedCommands":[{"appendContinuationItemsAction":
      {"continuationItems":[{"itemSectionRenderer":{"contents":[{"videoRenderer":{"videoId":"nq9TVmwWoOM","title":{"runs":[{"text":"Otra"}]},
      "ownerText":{"runs":[{"text":"Canal"}]},"lengthText":{"simpleText":"2:05"}}}]}},
      {"continuationItemRenderer":{"continuationEndpoint":{"continuationCommand":{"token":"PAGINA3"}}}}]}}]})json");
    check (more.videos.size() == 1 && more.videos[0].id == "nq9TVmwWoOM" && more.videos[0].seconds == 125.0 && more.continuation == "PAGINA3",
           "mas resultados: videos y continuacion", (double) more.videos.size(), 1.0);
}

// --- Limitador del master, pre-escucha y audifonos por otra tarjeta -----------------------------

void testLimiterAndHeadphones()
{
    std::printf ("== Limitador, pre-escucha y audifonos ==\n");
    juce::AudioFormatManager formats;
    midj::Deck a (formats), b (formats);
    midj::Mixer mixer (a, b);
    mixer.prepareToPlay (blockSize, sr);
    auto sine = [] (double hz, float amplitude)
    {
        juce::AudioBuffer<float> s (1, (int) (sr * 10.0));
        for (int i = 0; i < s.getNumSamples(); ++i)
            s.setSample (0, i, amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr));
        return s;
    };
    a.loadBuffer (sine (100.0, 0.5f), sr, "A", { 120.0, 0.0 });
    b.loadBuffer (sine (1000.0, 0.5f), sr, "B", { 120.0, 0.0 });
    a.play();
    b.play();

    // Medio segundo de audio: pico del master y RMS del master y de los audifonos (ultimo cuarto).
    struct Result { float masterPeak; double masterRms, cueRms; };
    auto run = [&]
    {
        juce::AudioBuffer<float> out (2, blockSize);
        Result r { 0.0f, 0.0, 0.0 };
        double m = 0.0, c = 0.0;
        int count = 0;
        const int blocks = (int) (sr * 0.5) / blockSize;
        for (int k = 0; k < blocks; ++k)
        {
            juce::AudioSourceChannelInfo info (&out, 0, blockSize);
            mixer.getNextAudioBlock (info);
            if (k >= 2)
                r.masterPeak = juce::jmax (r.masterPeak, out.getMagnitude (0, blockSize));
            if (k >= blocks * 3 / 4)
                for (int i = 0; i < blockSize; ++i, ++count)
                {
                    m += (double) out.getSample (0, i) * out.getSample (0, i);
                    c += (double) mixer.getCueBuffer().getSample (0, i) * mixer.getCueBuffer().getSample (0, i);
                }
        }
        r.masterRms = std::sqrt (m / count);
        r.cueRms = std::sqrt (c / count);
        return r;
    };

    // Limitador: A con +12 dB de ganancia daria picos de 2,0; con el limitador no pasan del techo.
    mixer.setCrossfader (0.0f);
    mixer.getStrip (0).setGain (1.0f);
    mixer.setLimiterEnabled (false);
    const auto loud = run();
    check (loud.masterPeak > 1.5f, "limitador apagado: el master pasa de 1", loud.masterPeak, 2.0);
    mixer.setLimiterEnabled (true);
    const auto limited = run();
    check (limited.masterPeak <= midj::Mixer::limiterCeiling + 0.001f, "limitador: el master no pasa de -0,3 dBFS",
           limited.masterPeak, midj::Mixer::limiterCeiling);
    check (mixer.takeLimiterGain() < 0.6f, "limitador: avisa de que esta recortando", 0.5, 0.5);
    mixer.getStrip (0).setGain (midj::ChannelStrip::defaultKnob);
    run();
    const auto normal = run();
    check (normal.masterPeak > 0.45f && normal.masterPeak < 0.52f, "limitador: sin picos altos no toca la senal", normal.masterPeak, 0.5);

    // Sin pre-escucha los audifonos oyen el master.
    mixer.setHeadphoneVolume (midj::Mixer::defaultMasterVolume);   // ganancia 1
    const auto plain = run();
    check (std::abs (plain.cueRms - plain.masterRms) < 0.01, "audifonos sin pre-escucha = master", plain.cueRms, plain.masterRms);

    // Pre-escucha de B con el crossfader en A y el volumen de B abajo: B solo en los audifonos.
    mixer.setPfl (1, true);
    mixer.setCueMix (0.0f);
    mixer.getStrip (1).setVolume (0.0f);
    run();
    const auto cued = run();
    check (std::abs (cued.cueRms - 0.3536) < 0.02, "pre-escucha: B en los audifonos (antes de su volumen)", cued.cueRms, 0.3536);
    check (std::abs (cued.masterRms - 0.3536) < 0.02, "pre-escucha: el master sigue siendo solo A", cued.masterRms, 0.3536);
    // Mezcla cue / master a la mitad: las dos senales a la mitad (100 Hz y 1 kHz no se anulan).
    mixer.setCueMix (0.5f);
    run();
    const auto half = run();
    check (std::abs (half.cueRms - 0.25) < 0.02, "pre-escucha: mezcla cue/master a la mitad", half.cueRms, 0.25);
    mixer.setPfl (1, false);

    // Audifonos por otra tarjeta: 44,1 kHz entran, 48 kHz salen; un La de 440 Hz sigue en 440.
    midj::HeadphoneOutput hp;
    hp.prepare (sr);
    const int outBlock = 480;
    juce::AudioBuffer<float> in (2, blockSize), out (2, (int) (48000 * 2.0));
    int written = 0, outDone = 0;
    double phase = 0.0;
    while (outDone + outBlock <= out.getNumSamples())
    {
        // La entrada va por delante lo justo (como dos tarjetas con su propio ritmo).
        while (written * 48000.0 / sr < outDone + outBlock * 2)
        {
            for (int i = 0; i < blockSize; ++i, phase += 440.0 / sr)
                in.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase)),
                in.setSample (1, i, in.getSample (0, i));
            hp.push (in.getReadPointer (0), in.getReadPointer (1), blockSize);
            written += blockSize;
        }
        hp.pullForTest (out.getWritePointer (0, outDone), out.getWritePointer (1, outDone), outBlock, 48000.0);
        outDone += outBlock;
    }
    const double freq = sineFrequency (out, 48000, out.getNumSamples()) * 48000.0 / sr;   // sineFrequency cuenta a 44,1 kHz
    check (std::abs (freq - 440.0) < 2.0, "audifonos por otra tarjeta: 440 Hz siguen en 440 Hz", freq, 440.0);
    check (out.getMagnitude (0, 48000, 48000) > 0.45f, "audifonos por otra tarjeta: sin cortes tras llenarse", out.getMagnitude (0, 48000, 48000), 0.5);
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
        formats.registerFormat (new midj::MediaFoundationAudioFormat(), false);   // MP4, M4A...
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
    if (args[0] == "--sample" && args.size() >= 2)   // datos de un sonido para el sampler (.vdjsample o audio)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        formats.registerFormat (new midj::MediaFoundationAudioFormat(), false);   // MP4, M4A...
        const auto s = midj::SampleData::loadFile (juce::File (args[1]), formats);
        if (s == nullptr)
        {
            std::printf ("No se pudo leer\n");
            return 1;
        }
        static const char* modes[] = { "libre", "sincronizado", "bloqueado" };
        std::printf ("%s: %.3f s a %.0f Hz, %.2f BPM (%d pulsos), primer pulso %.4f s, bucle %s, tempo %s, ganancia %.2f\n",
                     s->name.toRawUTF8(), s->lengthSeconds(), s->sampleRate, s->bpm, s->lengthBeats(), s->firstBeat,
                     s->loop ? "si" : "no", modes[juce::jlimit (0, 2, s->tempoMode)], s->gain);
        return 0;
    }
    if (args[0] == "--mixpoints")   // puntos de automix de canciones reales
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        formats.registerFormat (new midj::MediaFoundationAudioFormat(), false);   // MP4, M4A...
        for (int i = 1; i < args.size(); ++i)
        {
            double rate = 0.0;
            auto audio = readFile (formats, juce::File (args[i]), rate);
            if (audio == nullptr)
                continue;
            const auto tempo = midj::BpmDetector::analyse (*audio, rate);
            const auto p = midj::MixPointAnalyser::analyse (*audio, rate, tempo);
            std::printf ("%s\n  real %.3f - %.3f   fade %.3f - %.3f   tempo %.3f - %.3f   (%.2f BPM)\n",
                         juce::File (args[i]).getFileName().toRawUTF8(), p.realStart, p.realEnd, p.fadeStart, p.fadeEnd,
                         p.tempoStart, p.tempoEnd, tempo.bpm);
        }
        return 0;
    }
    if (args[0] == "--stems" && args.size() >= 2)
        return stemsFile (args[1], args[2] == "gpu", args.size() >= 4 ? args[3].getDoubleValue() : 30.0,
                          juce::File ("D:/Hamed/VirtualDJ/Pruebas stems"));
    if (args[0] == "--video" && args.size() >= 2)
        return videoInfo (juce::File (args[1]));
    if (args[0] == "--benchmark")
    {
        benchmarkKeyLock (midj::TimeStretcher::Engine::finer);
        benchmarkKeyLock (midj::TimeStretcher::Engine::faster);
        benchmarkEffects();
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
    testLimiterAndHeadphones();
    testLibraryLists();
    testYouTubeSearch();
    testAlphaVideo();

    std::printf ("== Video ==\n");
    testVideo();
    testCompositor();
    testVideoMix();
    testScenes();
    testCapture();
    testVisualizer();
    testAuxAudio();
    testMediaSource();
    testGpuFrames();

    std::printf ("== Stems con IA ==\n");
    testStemStft();
    testStemModel();
    testDeckStems();
    testStemsService();

    std::printf ("== Automix ==\n");
    testMixPoints();
    testAutomixQueueMove();
    testAutomixTempo();
    testAutomixFadeAndMixNow();

    std::printf ("== Sampler ==\n");
    testSampleData();
    testSampler();
    testSamplerRecording();

    std::printf ("== Controladoras MIDI ==\n");
    testMidiValues();
    testMidiController();

    std::printf ("== Efectos ==\n");
    testEffectsCommon();
    testEcho();
    testBitCrushAndGate();
    testEffectRack();

    std::printf ("== Biblioteca: etiquetas, SQLite, busqueda y analisis ==\n");
    testTagReader();
    testLibrary();

    std::printf ("== Keylock: tempo sin cambiar el tono ==\n");
    testKeyLock();

    std::printf ("== Deck: reproduccion, CUE y bucles ==\n");
    testDeck();
    testDeckPlayWhileLoading();
    std::printf ("== SYNC ==\n");
    testSync();

    std::printf ("\n%s (%d fallos)\n", failures == 0 ? "TODO OK" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}
