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
}

int main()
{
    const juce::ScopedJuceInitialiser_GUI juce;   // los decks avisan a la interfaz con mensajes

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

    std::printf ("== Deck: reproduccion, CUE y bucles ==\n");
    testDeck();
    std::printf ("== SYNC ==\n");
    testSync();

    std::printf ("\n%s (%d fallos)\n", failures == 0 ? "TODO OK" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}
