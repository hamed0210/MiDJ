// Pruebas automaticas del motor. Se ejecutan con: ctest --preset debug
// Devuelven codigo 0 si todo pasa y 1 si algo falla. No necesitan tarjeta de sonido:
// el audio se "reproduce" pidiendo bloques al deck directamente.
#include "Engine/BpmDetector.h"
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

    std::printf ("== Deck: reproduccion, CUE y bucles ==\n");
    testDeck();
    std::printf ("== SYNC ==\n");
    testSync();

    std::printf ("\n%s (%d fallos)\n", failures == 0 ? "TODO OK" : "HAY FALLOS", failures);
    return failures == 0 ? 0 : 1;
}
