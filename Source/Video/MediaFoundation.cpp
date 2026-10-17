#include "Video/MediaFoundation.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <atomic>
#include <mutex>
#include <vector>

#pragma comment (lib, "d3d11.lib")
#pragma comment (lib, "mfplat.lib")
#pragma comment (lib, "mfreadwrite.lib")
#pragma comment (lib, "mfuuid.lib")
#pragma comment (lib, "ole32.lib")
#pragma comment (lib, "propsys.lib")

using Microsoft::WRL::ComPtr;

namespace midj
{
namespace
{
    constexpr double hundredNs = 1.0e7;   // Media Foundation mide el tiempo en unidades de 100 ns

    juce::File fileOf (juce::InputStream* stream)
    {
        if (auto* f = dynamic_cast<juce::FileInputStream*> (stream))
            return f->getFile();
        return {};
    }

    // d3dManager: decodificar y convertir en la tarjeta grafica (DXVA) en vez de en la CPU.
    ComPtr<IMFSourceReader> openReader (const juce::File& file, bool advancedVideo, IUnknown* d3dManager = nullptr)
    {
        if (! MediaFoundation::initialiseThisThread())
            return nullptr;
        ComPtr<IMFAttributes> attributes;
        if (FAILED (MFCreateAttributes (&attributes, 2)))
            return nullptr;
        if (advancedVideo)   // conversion de color y cambio de tamano dentro de Media Foundation
            attributes->SetUINT32 (MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
        if (d3dManager != nullptr)
        {
            attributes->SetUnknown (MF_SOURCE_READER_D3D_MANAGER, d3dManager);
            attributes->SetUINT32 (MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        }
        ComPtr<IMFSourceReader> reader;
        if (FAILED (MFCreateSourceReaderFromURL (file.getFullPathName().toWideCharPointer(), attributes.Get(), &reader)))
            return nullptr;
        return reader;
    }

    double durationOf (IMFSourceReader* reader)
    {
        PROPVARIANT var;
        PropVariantInit (&var);
        double seconds = 0.0;
        if (SUCCEEDED (reader->GetPresentationAttribute ((DWORD) MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var)))
            seconds = (double) var.uhVal.QuadPart / hundredNs;
        PropVariantClear (&var);
        return seconds;
    }

    bool seekTo (IMFSourceReader* reader, double seconds)
    {
        PROPVARIANT var;
        PropVariantInit (&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = (LONGLONG) (juce::jmax (0.0, seconds) * hundredNs);
        const bool ok = SUCCEEDED (reader->SetCurrentPosition (GUID_NULL, var));
        PropVariantClear (&var);
        return ok;
    }
}

// --- Inicio de Media Foundation -------------------------------------------------------------

bool MediaFoundation::initialiseThisThread()
{
    // COM en el hilo (si ya estaba en otro modo, como el hilo de interfaz de JUCE, vale igual).
    thread_local bool comReady = false;   // una vez por hilo
    if (! comReady)
    {
        CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        comReady = true;
    }
    static std::once_flag once;
    static bool started = false;
    std::call_once (once, [] { started = SUCCEEDED (MFStartup (MF_VERSION, MFSTARTUP_FULL)); });
    return started;
}

juce::String MediaFoundation::extensions()
{
    return "mp4;m4a;mov;m4v;wmv;wma;aac;3gp";
}

// --- Audio --------------------------------------------------------------------------------

namespace
{
    class MediaFoundationReader : public juce::AudioFormatReader
    {
    public:
        MediaFoundationReader (juce::InputStream* stream, ComPtr<IMFSourceReader> r)
            : juce::AudioFormatReader (stream, "Media Foundation"), reader (std::move (r)) {}

        bool initialise()
        {
            // Solo la primera pista de audio, decodificada a float.
            reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
            if (FAILED (reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE)))
                return false;
            ComPtr<IMFMediaType> type;
            MFCreateMediaType (&type);
            type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            type->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_Float);
            if (FAILED (reader->SetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type.Get())))
                return false;
            ComPtr<IMFMediaType> actual;
            if (FAILED (reader->GetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual)))
                return false;
            channels = (int) MFGetAttributeUINT32 (actual.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0);
            sampleRate = MFGetAttributeUINT32 (actual.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
            if (channels <= 0 || sampleRate <= 0.0)
                return false;
            numChannels = (unsigned int) channels;
            bitsPerSample = 32;
            usesFloatingPointData = true;
            lengthInSamples = (juce::int64) (durationOf (reader.Get()) * sampleRate);
            return lengthInSamples > 0;
        }

        bool readSamples (int* const* destSamples, int numDestChannels, int startOffsetInDestBuffer,
                          juce::int64 startSampleInFile, int numSamples) override
        {
            MediaFoundation::initialiseThisThread();   // se puede leer desde otro hilo que el que lo abrio
            // Si se pide algo que no sigue a lo ya decodificado, se busca.
            if (startSampleInFile < pendingStart || startSampleInFile > pendingStart + pendingFrames() + (juce::int64) sampleRate)
                seek (startSampleInFile);

            int done = 0;
            while (done < numSamples)
            {
                const juce::int64 want = startSampleInFile + done;
                const juce::int64 offset = want - pendingStart;
                if (offset < 0 && ! pending.empty())
                {
                    // Hueco: lo decodificado empieza despues de lo pedido (busqueda inexacta): silencio.
                    const int k = (int) juce::jmin ((juce::int64) (numSamples - done), -offset);
                    for (int ch = 0; ch < numDestChannels; ++ch)
                        if (destSamples[ch] != nullptr)
                            juce::FloatVectorOperations::clear (reinterpret_cast<float*> (destSamples[ch]) + startOffsetInDestBuffer + done, k);
                    done += k;
                    continue;
                }
                if (offset >= 0 && offset < pendingFrames())
                {
                    const int k = (int) juce::jmin ((juce::int64) (numSamples - done), pendingFrames() - offset);
                    for (int ch = 0; ch < numDestChannels; ++ch)
                        if (destSamples[ch] != nullptr)
                        {
                            auto* out = reinterpret_cast<float*> (destSamples[ch]) + startOffsetInDestBuffer + done;
                            const int src = juce::jmin (ch, channels - 1);
                            for (int i = 0; i < k; ++i)
                                out[i] = pending[(size_t) ((offset + i) * channels + src)];
                        }
                    done += k;
                    continue;
                }
                if (! decodeNext())
                {
                    // Final del archivo: lo que falte, silencio.
                    for (int ch = 0; ch < numDestChannels; ++ch)
                        if (destSamples[ch] != nullptr)
                            juce::FloatVectorOperations::clear (reinterpret_cast<float*> (destSamples[ch]) + startOffsetInDestBuffer + done,
                                                                numSamples - done);
                    break;
                }
            }
            return true;
        }

    private:
        juce::int64 pendingFrames() const { return (juce::int64) pending.size() / juce::jmax (1, channels); }

        void seek (juce::int64 sample)
        {
            // Un poco antes: el primer paquete tras buscar sale mal (al AAC le falta el solapamiento
            // con el anterior); asi cae antes de lo pedido y se descarta al seguir decodificando.
            const juce::int64 preroll = 8192;
            seekTo (reader.Get(), (double) juce::jmax ((juce::int64) 0, sample - preroll) / sampleRate);
            pending.clear();
            pendingStart = sample;
            exact = false;   // el primer paquete despues de buscar dice por su marca de tiempo donde cae
            ended = false;
        }

        bool decodeNext()
        {
            if (ended)
                return false;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            if (FAILED (reader->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, &timestamp, &sample))
                || (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
            {
                ended = true;
                return false;
            }
            if (sample == nullptr)
                return true;   // paquete vacio (cambio de formato, etc.): sigue
            ComPtr<IMFMediaBuffer> buffer;
            sample->ConvertToContiguousBuffer (&buffer);
            BYTE* data = nullptr;
            DWORD length = 0;
            buffer->Lock (&data, nullptr, &length);
            const auto* floats = reinterpret_cast<const float*> (data);
            const size_t count = length / sizeof (float);

            const juce::int64 at = (juce::int64) std::llround ((double) timestamp / hundredNs * sampleRate);
            if (! exact)
            {
                // Tras buscar: el paquete empieza en 'at' (antes de lo pedido). Se coloca donde toca.
                pendingStart = at;
                pending.assign (floats, floats + count);
                exact = true;
            }
            else
            {
                // Seguido: se descarta lo ya leido para que el bufer no crezca.
                const juce::int64 end = pendingStart + pendingFrames();
                pending.erase (pending.begin(), pending.end());
                pendingStart = end;
                pending.assign (floats, floats + count);
            }
            buffer->Unlock();
            return true;
        }

        ComPtr<IMFSourceReader> reader;
        int channels = 0;
        std::vector<float> pending;   // muestras intercaladas decodificadas, desde pendingStart
        juce::int64 pendingStart = 0;
        bool exact = false, ended = false;
    };
}

namespace
{
    juce::StringArray dottedExtensions()
    {
        juce::StringArray list;
        for (const auto& e : juce::StringArray::fromTokens (MediaFoundation::extensions(), ";", ""))
            list.add ("." + e);
        return list;
    }
}

MediaFoundationAudioFormat::MediaFoundationAudioFormat()
    : juce::AudioFormat ("Media Foundation", dottedExtensions())
{
}

juce::AudioFormatReader* MediaFoundationAudioFormat::createReaderFor (juce::InputStream* source, bool deleteStreamIfOpeningFails)
{
    const auto file = fileOf (source);
    if (file.existsAsFile() && file.hasFileExtension (MediaFoundation::extensions()))
        if (auto reader = openReader (file, false))
        {
            auto r = std::make_unique<MediaFoundationReader> (source, std::move (reader));
            if (r->initialise())
                return r.release();   // el lector se queda con el stream
            r->input = nullptr;       // no borrar el stream: lo decide el que llama
        }
    if (deleteStreamIfOpeningFails)
        delete source;
    return nullptr;
}

// --- Video --------------------------------------------------------------------------------

struct VideoDecoder::Impl
{
    ComPtr<IMFSourceReader> reader;
    int width = 0, height = 0, stride = 0;
    double frameRate = 25.0, duration = 0.0;
    double currentTime = -1.0, currentEnd = -1.0;
    bool ended = false;
    bool catchingUp = false;    // tras buscar, aun no se ha llegado al instante pedido
    double seekTarget = 0.0;
    bool gpu = false;           // los fotogramas llegan de la tarjeta grafica
    ComPtr<ID3D11Device> device;              // solo con GPU: se mantienen vivos con el lector
    ComPtr<IMFDXGIDeviceManager> deviceManager;

    // Copia un fotograma RGB32 a la imagen. En la GPU la memoria es una textura con su propio
    // paso por fila: IMF2DBuffer la trae a la CPU y da ese paso; si no, el bufer contiguo.
    bool copyFrame (IMFSample* sample, juce::Image& image)
    {
        ComPtr<IMFMediaBuffer> buffer;
        ComPtr<IMF2DBuffer> buffer2d;
        BYTE* scan0 = nullptr;
        LONG pitch = 0;
        bool locked2d = false;
        if (SUCCEEDED (sample->GetBufferByIndex (0, &buffer)) && SUCCEEDED (buffer.As (&buffer2d))
            && SUCCEEDED (buffer2d->Lock2D (&scan0, &pitch)))
        {
            locked2d = true;   // scan0 = fila de arriba (pitch < 0 si la imagen va de abajo arriba)
        }
        else
        {
            buffer.Reset();
            if (FAILED (sample->ConvertToContiguousBuffer (&buffer)))
                return false;
            BYTE* data = nullptr;
            DWORD size = 0;
            if (FAILED (buffer->Lock (&data, nullptr, &size)))
                return false;
            if ((size_t) std::abs (stride) * (size_t) height > size)
            {
                buffer->Unlock();
                return false;
            }
            pitch = stride;
            scan0 = stride >= 0 ? data : data + (size_t) (height - 1) * (size_t) -stride;
        }
        {
            juce::Image::BitmapData out (image, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < height; ++y)
            {
                // RGB32 de Media Foundation = B, G, R, X en memoria: como PixelARGB de JUCE.
                auto* src = reinterpret_cast<const uint32_t*> (scan0 + (std::ptrdiff_t) y * pitch);
                auto* dst = reinterpret_cast<uint32_t*> (out.getLinePointer (y));
                for (int x = 0; x < width; ++x)
                    dst[x] = src[x] | 0xff000000u;   // opaco
            }
        }
        if (locked2d) buffer2d->Unlock2D();
        else          buffer->Unlock();
        return true;
    }

    bool readFrameAtOrAfter (double target, juce::Image& image, int maxFrames)
    {
        for (int read = 1;; ++read)
        {
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            if (FAILED (reader->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &timestamp, &sample))
                || (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
            {
                ended = true;
                return false;
            }
            if (sample == nullptr)
                continue;
            LONGLONG length = 0;
            sample->GetSampleDuration (&length);
            const double start = (double) timestamp / hundredNs;
            const double end = start + (length > 0 ? (double) length / hundredNs : 1.0 / frameRate);
            const bool early = end <= target;
            if (early && (maxFrames <= 0 || read < maxFrames))
                continue;   // aun antes de lo pedido: se salta sin copiar

            if (! image.isValid() || image.getWidth() != width || image.getHeight() != height)
                image = juce::Image (juce::Image::ARGB, width, height, false, juce::SoftwareImageType());   // se escribe en la CPU
            if (! copyFrame (sample.Get(), image))
                continue;
            currentTime = start;
            currentEnd = end;
            if (! early)
                catchingUp = false;
            return true;
        }
    }
};

VideoDecoder::VideoDecoder (std::unique_ptr<Impl> i) : impl (std::move (i)) {}
VideoDecoder::~VideoDecoder() = default;

namespace
{
    // Dispositivo Direct3D 11 con video (DXVA) y su gestor para Media Foundation.
    bool createGpuDevice (ComPtr<ID3D11Device>& device, ComPtr<IMFDXGIDeviceManager>& manager)
    {
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        if (FAILED (D3D11CreateDevice (nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                       D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                       levels, (UINT) std::size (levels), D3D11_SDK_VERSION, &device, nullptr, nullptr)))
            return false;
        // Media Foundation usa el dispositivo desde sus propios hilos.
        ComPtr<ID3D10Multithread> multithread;
        if (SUCCEEDED (device.As (&multithread)))
            multithread->SetMultithreadProtected (TRUE);
        UINT token = 0;
        return SUCCEEDED (MFCreateDXGIDeviceManager (&token, &manager))
            && SUCCEEDED (manager->ResetDevice (device.Get(), token));
    }
}

std::unique_ptr<VideoDecoder> VideoDecoder::open (const juce::File& file, int maxWidth, juce::String& error, bool allowGpu)
{
    if (allowGpu)
    {
        juce::String gpuError;
        if (auto decoder = openWith (file, maxWidth, gpuError, true))
            return decoder;
    }
    return openWith (file, maxWidth, error, false);
}

std::unique_ptr<VideoDecoder> VideoDecoder::openWith (const juce::File& file, int maxWidth, juce::String& error, bool useGpu)
{
    if (! MediaFoundation::initialiseThisThread())
    {
        error = "Media Foundation no esta disponible";
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    if (useGpu && ! createGpuDevice (impl->device, impl->deviceManager))
    {
        error = "Sin Direct3D 11 con video";
        return nullptr;
    }
    impl->reader = openReader (file, true, useGpu ? impl->deviceManager.Get() : nullptr);
    if (impl->reader == nullptr)
    {
        error = "Media Foundation no puede abrir " + file.getFileName();
        return nullptr;
    }
    auto* r = impl->reader.Get();
    r->SetStreamSelection ((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED (r->SetStreamSelection ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE)))
    {
        error = "El archivo no tiene video";
        return nullptr;
    }
    ComPtr<IMFMediaType> native;
    if (FAILED (r->GetNativeMediaType ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &native)))
    {
        error = "El archivo no tiene video";
        return nullptr;
    }
    UINT32 w = 0, h = 0, num = 0, den = 0;
    MFGetAttributeSize (native.Get(), MF_MT_FRAME_SIZE, &w, &h);
    if (SUCCEEDED (MFGetAttributeRatio (native.Get(), MF_MT_FRAME_RATE, &num, &den)) && num > 0 && den > 0)
        impl->frameRate = (double) num / den;
    if (w == 0 || h == 0)
    {
        error = "Video sin tamano";
        return nullptr;
    }

    // RGB32 reducido a maxWidth (pares); si Media Foundation no puede reducir, al tamano original.
    UINT32 outW = w, outH = h;
    if (maxWidth > 0 && (int) w > maxWidth)
    {
        outW = (UINT32) maxWidth & ~1u;
        outH = (UINT32) std::lround ((double) h * outW / w) & ~1u;
    }
    auto setType = [&] (UINT32 tw, UINT32 th)
    {
        ComPtr<IMFMediaType> type;
        MFCreateMediaType (&type);
        type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        MFSetAttributeSize (type.Get(), MF_MT_FRAME_SIZE, tw, th);
        return SUCCEEDED (r->SetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type.Get()));
    };
    if (! setType (outW, outH) && ! setType (w, h))
    {
        error = "Media Foundation no puede convertir este video a RGB";
        return nullptr;
    }
    ComPtr<IMFMediaType> actual;
    r->GetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual);
    MFGetAttributeSize (actual.Get(), MF_MT_FRAME_SIZE, &w, &h);
    impl->width = (int) w;
    impl->height = (int) h;
    impl->stride = (int) (INT32) MFGetAttributeUINT32 (actual.Get(), MF_MT_DEFAULT_STRIDE, (UINT32) (w * 4));
    impl->duration = durationOf (r);

    if (useGpu)
    {
        // Con GPU se comprueba que de verdad sale un fotograma (si el controlador falla, a la CPU)
        // y si llega como textura de la tarjeta (decodificado alli) o como memoria normal.
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        for (int tries = 0; tries < 10 && sample == nullptr; ++tries)
            if (FAILED (r->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &timestamp, &sample))
                || (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
                break;
        juce::Image test (juce::Image::ARGB, (int) w, (int) h, false, juce::SoftwareImageType());
        if (sample == nullptr || ! impl->copyFrame (sample.Get(), test))
        {
            error = "La GPU no entrega fotogramas";
            return nullptr;
        }
        ComPtr<IMFMediaBuffer> buffer;
        ComPtr<IMFDXGIBuffer> dxgi;
        impl->gpu = SUCCEEDED (sample->GetBufferByIndex (0, &buffer)) && SUCCEEDED (buffer.As (&dxgi));
        if (! impl->gpu)
        {
            error = "Media Foundation no uso la GPU";
            return nullptr;   // mejor el camino normal de CPU, ya probado
        }
        seekTo (r, 0.0);
    }
    return std::unique_ptr<VideoDecoder> (new VideoDecoder (std::move (impl)));
}

bool VideoDecoder::isUsingGpu() const { return impl->gpu; }

int VideoDecoder::getWidth() const          { return impl->width; }
int VideoDecoder::getHeight() const         { return impl->height; }
double VideoDecoder::getFrameRate() const   { return impl->frameRate; }
double VideoDecoder::getDuration() const    { return impl->duration; }
double VideoDecoder::getCurrentFrameTime() const { return impl->currentTime; }

bool VideoDecoder::frameAt (double seconds, juce::Image& image, int maxFrames)
{
    auto& m = *impl;
    if (seconds >= m.currentTime && seconds < m.currentEnd && image.isValid())
        return true;   // sigue siendo el mismo fotograma
    // Hacia atras, o muy adelante: buscar (Media Foundation va al fotograma clave anterior).
    // Mientras se alcanza lo pedido tras una busqueda no se vuelve a buscar, salvo otro salto:
    // si el fotograma clave queda lejos, buscar otra vez volveria al mismo sitio.
    const bool farAhead = m.catchingUp ? seconds > m.seekTarget + 5.0 : seconds > m.currentEnd + 2.0;
    if (seconds < m.currentTime || farAhead || m.ended)
    {
        seekTo (m.reader.Get(), seconds);
        m.ended = false;
        m.currentTime = m.currentEnd = -1.0;
        m.catchingUp = true;
        m.seekTarget = seconds;
    }
    return m.readFrameAtOrAfter (seconds, image, maxFrames);
}

bool VideoDecoder::isCatchingUp() const { return impl->catchingUp; }
}
