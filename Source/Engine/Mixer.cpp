#include "Engine/Mixer.h"

namespace midj
{
Mixer::Mixer (Deck& left, Deck& right) : deckA (left), deckB (right) {}

float Mixer::leftGain (float x)
{
    // VirtualDJ "smooth": curva 0.333 -> ganancia = min(1, (1 - x) / 0.667)
    return juce::jmin (1.0f, (1.0f - x) / 0.667f);
}

TempoInfo Mixer::tempoFor (const Deck& deck, double sampleRate)
{
    TempoInfo t;
    const double bpm = deck.getCurrentBpm();
    if (bpm > 0.0 && deck.isPlaying())
    {
        t.samplesPerBeat = sampleRate * 60.0 / bpm;
        t.beatAtStart = (deck.getPositionSeconds() - deck.getFirstBeat()) * deck.getOriginalBpm() / 60.0;
        t.synced = true;
    }
    else
    {
        t.samplesPerBeat = sampleRate * (bpm > 0.0 ? 60.0 / bpm : 0.5);
    }
    return t;
}

void Mixer::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    outputRate = sampleRate;
    deckA.prepareToPlay (samplesPerBlockExpected, sampleRate);
    deckB.prepareToPlay (samplesPerBlockExpected, sampleRate);
    stripA.prepare (sampleRate, samplesPerBlockExpected);
    stripB.prepare (sampleRate, samplesPerBlockExpected);
    effectsA.prepare (sampleRate, samplesPerBlockExpected);
    effectsB.prepare (sampleRate, samplesPerBlockExpected);
    sampler.prepare (sampleRate, samplesPerBlockExpected);
    tap.prepare (sampleRate);
    scratch.setSize (2, samplesPerBlockExpected);
}

void Mixer::releaseResources()
{
    deckA.releaseResources();
    deckB.releaseResources();
}

void Mixer::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    const int n = info.numSamples;
    if (scratch.getNumSamples() < n)
        scratch.setSize (2, n, false, false, true);   // solo crece si el driver cambia el tamano de bloque

    // Deck A directamente en la salida, deck B en un bufer temporal; cada uno por sus efectos
    // y su canal. El tempo se toma antes de que el deck avance: es el del inicio del bloque.
    const auto tempoA = tempoFor (deckA, outputRate), tempoB = tempoFor (deckB, outputRate);
    deckA.getNextAudioBlock (info);
    juce::AudioSourceChannelInfo bInfo (&scratch, 0, n);
    deckB.getNextAudioBlock (bInfo);
    auto& outBuffer = *info.buffer;
    const int lastOut = outBuffer.getNumChannels() - 1;
    effectsA.process (outBuffer.getWritePointer (0, info.startSample), outBuffer.getWritePointer (juce::jmin (1, lastOut), info.startSample), n, tempoA);
    effectsB.process (scratch.getWritePointer (0), scratch.getWritePointer (1), n, tempoB);
    // El sampler graba lo que sale de cada deck con sus efectos (antes del EQ y el volumen).
    sampler.capture (0, outBuffer.getReadPointer (0, info.startSample), outBuffer.getReadPointer (juce::jmin (1, lastOut), info.startSample), n, tempoA);
    sampler.capture (1, scratch.getReadPointer (0), scratch.getReadPointer (1), n, tempoB);
    stripA.process (*info.buffer, info.startSample, n);
    stripB.process (scratch, 0, n);

    const float x = crossfader.load();
    const float gainA = leftGain (x), gainB = rightGain (x);

    auto& out = *info.buffer;
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
    {
        // Rampa de ganancia dentro del bloque para que mover el crossfader no haga clics.
        out.applyGainRamp (ch, info.startSample, n, lastGainA, gainA);
        out.addFromWithRamp (ch, info.startSample,
                             scratch.getReadPointer (juce::jmin (ch, scratch.getNumChannels() - 1)),
                             n, lastGainB, gainB);
    }
    lastGainA = gainA;
    lastGainB = gainB;

    // Sampler: referencia de tempo = el deck que suena (si suenan los dos, el que el crossfader
    // deja mas alto).
    Sampler::Reference ref;
    const TempoInfo* master = nullptr;
    if (tempoA.synced && tempoB.synced) master = gainA >= gainB ? &tempoA : &tempoB;
    else if (tempoA.synced)              master = &tempoA;
    else if (tempoB.synced)              master = &tempoB;
    if (master != nullptr)
    {
        ref.valid = true;
        ref.samplesPerBeat = master->samplesPerBeat;
        ref.bpm = outputRate * 60.0 / master->samplesPerBeat;
        ref.beatAtStart = master->beatAtStart;
    }
    sampler.process (out.getWritePointer (0, info.startSample), out.getWritePointer (juce::jmin (1, out.getNumChannels() - 1), info.startSample), n, ref);

    tap.push (out.getReadPointer (0, info.startSample), out.getNumChannels() > 1 ? out.getReadPointer (1, info.startSample) : nullptr, n);
    tap.setBeat (ref.valid, ref.valid ? ref.beatAtStart + n / ref.samplesPerBeat : 0.0, ref.bpm);
}
}
