#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>

#include <memory>
#include <vector>

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

    // El dispositivo Direct3D 11 del compositor (nullptr = ninguno). Con el, los decodificadores
    // que usan la tarjeta grafica entregan fotogramas que se quedan en ella (GpuVideoFrame): el
    // compositor los copia de textura a textura, sin pasar por la memoria normal (como OBS).
    void shareDevice (void* d3d11Device);
    bool hasSharedDevice();
}

// Un fotograma decodificado que sigue en la tarjeta grafica, en el dispositivo compartido:
// una textura RGB (o un trozo de un array de texturas). Mientras exista, Media Foundation no
// reutiliza esa textura.
class GpuVideoFrame
{
public:
    virtual ~GpuVideoFrame() = default;
    virtual void* getTexture() const = 0;   // ID3D11Texture2D*
    virtual int getSubresource() const = 0;
    int width = 0, height = 0;
};

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
    // allowGpu: decodificar en la tarjeta grafica (DXVA con Direct3D 11) si se puede; si falla
    // o el controlador no lo admite, en la CPU.
    static std::unique_ptr<VideoDecoder> open (const juce::File& file, int maxWidth, juce::String& error, bool allowGpu = true);
    ~VideoDecoder();

    int getWidth() const;
    int getHeight() const;
    double getFrameRate() const;
    double getDuration() const;
    bool isUsingGpu() const;   // decodificado en la tarjeta grafica

    // El fotograma que se ve en 'seconds'. false si no hay (final o error).
    // maxFrames > 0: decodifica como mucho esos fotogramas y entrega el ultimo aunque aun no
    // llegue (tras un salto el video avanza a saltos hasta alcanzar al audio, sin congelarse).
    bool frameAt (double seconds, juce::Image& image, int maxFrames = 0);
    // Lo mismo dejando el fotograma en la tarjeta grafica (solo con isOnSharedDevice()).
    bool frameAt (double seconds, std::shared_ptr<GpuVideoFrame>& frame, int maxFrames = 0);
    bool isOnSharedDevice() const;   // decodifica en el dispositivo del compositor
    bool isCatchingUp() const;   // tras un salto, aun por detras de lo pedido
    double getCurrentFrameTime() const;   // inicio del fotograma entregado

private:
    struct Impl;
    friend class VideoCaptureReader;   // usa su copia de fotogramas
    static std::unique_ptr<VideoDecoder> openWith (const juce::File& file, int maxWidth, juce::String& error, bool useGpu);
    explicit VideoDecoder (std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl;
};

// Un dispositivo de captura de video: capturadoras USB/HDMI y webcams (las que Windows ve con
// su controlador UVC o de Media Foundation; OBS usa DirectShow, que ademas ve algunas
// capturadoras antiguas). Se lee en un hilo propio: read() espera al siguiente fotograma.
class VideoCaptureReader
{
public:
    struct Device
    {
        juce::String name, id;   // id = enlace simbolico de Windows (no cambia entre arranques)
    };
    struct Format
    {
        int width = 0, height = 0;   // 0 = el predeterminado del dispositivo
        double fps = 0.0;            // 0 = el mas alto para ese tamano
        juce::String format;         // "NV12", "YUY2", "MJPG"...; vacio = cualquiera
        bool operator== (const Format&) const = default;
    };

    static std::vector<Device> devices();
    static std::vector<Format> formats (const juce::String& deviceId);

    // maxWidth: se reduce a ese ancho. allowGpu: decodificar y convertir en la tarjeta grafica.
    static std::unique_ptr<VideoCaptureReader> open (const juce::String& deviceId, const Format& wanted, int maxWidth,
                                                     bool allowGpu, juce::String& error);
    ~VideoCaptureReader();

    // Espera el siguiente fotograma (como mucho ~1 s) y lo copia. false = desconectado o error.
    bool read (juce::Image& image, bool flipVertically);
    // Lo mismo dejando el fotograma en la tarjeta grafica (solo con isOnSharedDevice()).
    bool read (std::shared_ptr<GpuVideoFrame>& frame);
    bool isOnSharedDevice() const;
    // Cualquier hilo: suelta el dispositivo; un read() que estaba esperando vuelve con false.
    void interrupt();
    Format getFormat() const;   // el que entrega el dispositivo (tamano antes de reducir)
    int getWidth() const;
    int getHeight() const;

private:
    struct Impl;
    explicit VideoCaptureReader (std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl;
};
}
