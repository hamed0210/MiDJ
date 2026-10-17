#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <memory>

namespace midj
{
// Un sonido del sampler, ya decodificado en memoria (estereo).
struct SampleData
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    double bpm = 0.0;            // 0 = sin tempo conocido
    double firstBeat = 0.0;      // segundos hasta el primer pulso dentro del sonido
    juce::String name;
    juce::File source;           // de donde salio (vacio = grabado en el momento)

    // Ajustes que trae el propio archivo (los .vdjsample de VirtualDJ los guardan).
    bool loop = false;
    int tempoMode = 0;           // 0 libre, 1 sincronizado, 2 bloqueado (Sampler::TempoMode)
    float gain = 1.0f;

    double lengthSeconds() const { return audio.getNumSamples() / sampleRate; }
    // Cuantos pulsos dura (redondeado), si tiene tempo.
    int lengthBeats() const;

    // Carga un archivo de audio (WAV, MP3, FLAC, Ogg, AIFF) o un .vdjsample de VirtualDJ.
    // Se guardan como mucho maxSeconds. nullptr si no se puede leer.
    static std::shared_ptr<SampleData> loadFile (const juce::File& file, juce::AudioFormatManager& formats,
                                                 double maxSeconds = 30.0);

    // Formato .vdjsample (VirtualDJ 8): cabecera binaria little-endian + audio Ogg + icono.
    //   0x00 "VDJ\0"  0x08 tamano de cabecera (= inicio del audio)  0x0C tamano del audio
    //   0x18 isloop (u8)  0x1C loopmode (0 flat, 1 pitched, 2 sync, 3 locked)
    //   0x20 BPM en segundos por pulso (float)  0x24 fase (float, s)
    //   0x28 inicio del fragmento (double, s)   0x30 duracion del fragmento (double, s)
    //   0x48 ganancia (float)
    static std::shared_ptr<SampleData> loadVdjSample (const juce::MemoryBlock& data, const juce::String& name,
                                                      juce::AudioFormatManager& formats);
};
}
