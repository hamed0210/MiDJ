#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <memory>

namespace midj
{
// Separacion de voz con IA (Spleeter 2 stems en ONNX, el de sherpa-onnx, con ONNX Runtime).
//
// Se hace por adelantado (no en tiempo real: VirtualDJ ya avisa de que este equipo no puede
// separar en directo). El resultado es solo la VOZ; lo instrumental es original - voz, asi las
// dos partes suman exactamente la cancion.
//
// Proceso (el mismo que sherpa-onnx): STFT de 4096 con salto 1024 y ventana de Hann a 44,1 kHz;
// los modelos de voz y de acompanamiento reciben la magnitud de los 1024 primeros bins en trozos
// de 512 fotogramas; mascara de voz = v^2 / (v^2 + a^2); por encima del bin 1024 (11 kHz) la voz
// es 0 (va todo a lo instrumental); STFT inversa.
class StemSeparator
{
public:
    enum class Device { cpu, gpu };   // gpu = DirectML (cualquier tarjeta con DirectX 12)

    StemSeparator();
    ~StemSeparator();

    // Carga vocals*.onnx y accompaniment*.onnx de la carpeta. false + error si falla.
    bool load (const juce::File& modelFolder, Device device, juce::String& error);
    bool isLoaded() const;
    juce::String describe() const;   // modelo, dispositivo y parametros de la STFT
    juce::String getMetadata() const; // todos los datos que trae el modelo (clave=valor)
    juce::String getLastError() const; // por que fallo la ultima separacion

    static constexpr double modelSampleRate = 44100.0;

    // Separa la voz de 'stereo' (a modelSampleRate). progress(0..1) se llama por cada trozo
    // y si devuelve false se cancela (y la funcion devuelve false).
    bool separateVocals (const juce::AudioBuffer<float>& stereo, juce::AudioBuffer<float>& vocals,
                         const std::function<bool (double)>& progress = {});

    // Lo mismo pero con una mascara cualquiera en vez de los modelos (pruebas de la STFT).
    // mask (bin, fotograma) -> 0..1.
    void separateWithMask (const juce::AudioBuffer<float>& stereo, juce::AudioBuffer<float>& out,
                           const std::function<float (int bin, int frame)>& mask);

    // Remuestreo de alta calidad (sinc con ventana) para llevar la cancion a 44,1 kHz y volver.
    static juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, double fromRate, double toRate);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
