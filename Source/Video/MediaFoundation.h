#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>

#include <memory>

// Video y audio de archivos MP4/MOV/WMV/M4A con Media Foundation (incluido en Windows: sin
// FFmpeg). VirtualDJ usa FFmpeg; aqui Media Foundation hace lo mismo con los codecs de Windows.
namespace midj
{
namespace MediaFoundation
{
    // Media Foundation y COM listos en el hilo que llama (se puede llamar muchas veces).
    bool initialiseThisThread();
    // Extensiones que se abren con Media Foundation.
    juce::String extensions();   // "mp4;m4a;mov;m4v;wmv;wma;aac;3gp"
}

// Formato de audio para JUCE: la pista de sonido de un video o un M4A/AAC/WMA.
class MediaFoundationAudioFormat : public juce::AudioFormat
{
public:
    MediaFoundationAudioFormat();
    juce::Array<int> getPossibleSampleRates() override { return {}; }
    juce::Array<int> getPossibleBitDepths() override { return {}; }
    bool canDoStereo() override { return true; }
    bool canDoMono() override { return true; }
    bool isCompressed() override { return true; }
    // Solo lectura y solo desde archivo (Media Foundation abre por ruta).
    juce::AudioFormatReader* createReaderFor (juce::InputStream* source, bool deleteStreamIfOpeningFails) override;
    juce::AudioFormatWriter* createWriterFor (juce::OutputStream*, double, unsigned int, int, const juce::StringPairArray&, int) override
    {
        return nullptr;   // MiDJ solo lee videos
    }
};

// Decodificador de video de un archivo: entrega el fotograma que toca en cada instante.
// Reproduciendo hacia delante decodifica seguido; si se salta atras o mas de 2 s adelante,
// busca (Media Foundation va al fotograma clave anterior y se decodifica hasta el instante).
class VideoDecoder
{
public:
    // maxWidth: se reduce a ese ancho (manteniendo la proporcion) para aligerar.
    static std::unique_ptr<VideoDecoder> open (const juce::File& file, int maxWidth, juce::String& error);
    ~VideoDecoder();

    int getWidth() const;
    int getHeight() const;
    double getFrameRate() const;
    double getDuration() const;

    // El fotograma que se ve en 'seconds'. false si no hay (final o error).
    // maxFrames > 0: decodifica como mucho esos fotogramas y entrega el ultimo aunque aun no
    // llegue (tras un salto el video avanza a saltos hasta alcanzar al audio, sin congelarse).
    bool frameAt (double seconds, juce::Image& image, int maxFrames = 0);
    bool isCatchingUp() const;   // tras un salto, aun por detras de lo pedido
    double getCurrentFrameTime() const;   // inicio del fotograma entregado

private:
    struct Impl;
    explicit VideoDecoder (std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl;
};
}
