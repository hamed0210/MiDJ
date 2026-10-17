#include "Engine/StemSeparator.h"

#include <juce_dsp/juce_dsp.h>

#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>

#include <cmath>
#include <array>
#include <complex>
#include <thread>
#include <vector>

namespace midj
{
namespace
{
    constexpr int segmentFrames = 512;   // fotogramas por trozo (fijo en el modelo de Spleeter)
    constexpr int modelBins = 1024;      // bins de frecuencia que ve el modelo (hasta 11 kHz)
}

struct StemSeparator::Impl
{
    // Parametros de la STFT (por defecto los de Spleeter; el modelo puede traer otros).
    int nfft = 4096, hop = 1024;
    bool center = true;
    juce::String deviceName, modelName, metadata, lastError;

    Ort::Env env { ORT_LOGGING_LEVEL_WARNING, "midj" };
    std::unique_ptr<Ort::Session> vocals, accompaniment;
    std::string vocalsIn, vocalsOut, accIn, accOut;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;

    void prepareStft()
    {
        int order = 0;
        while ((1 << order) < nfft)
            ++order;
        fft = std::make_unique<juce::dsp::FFT> (order);
        window.resize ((size_t) nfft);
        for (int i = 0; i < nfft; ++i)   // Hann periodica
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) nfft);
    }

    // STFT -> mascara por bloques de 512 fotogramas -> STFT inversa con suma solapada.
    // 'maskForSegment' recibe los espectros (canal, fotograma, bin) del trozo y escribe la mascara
    // (canal, fotograma, bin) de los 'bins' primeros bins; el resto queda a 0.
    template <typename MaskFn>
    bool process (const juce::AudioBuffer<float>& in, juce::AudioBuffer<float>& out, MaskFn&& maskForSegment,
                  const std::function<bool (double)>& progress)
    {
        const int n = in.getNumSamples();
        const int pad = center ? nfft / 2 : 0;
        const int frames = center ? 1 + n / hop : juce::jmax (0, 1 + (n - nfft) / hop);
        const int bins = nfft / 2 + 1;
        const int segments = (frames + segmentFrames - 1) / segmentFrames;

        out.setSize (2, n);
        out.clear();
        // Suma solapada de los dos canales y suma de ventanas al cuadrado (para normalizar).
        std::array<std::vector<float>, 2> acc;
        for (auto& a : acc)
            a.assign ((size_t) (n + 2 * pad + nfft), 0.0f);
        std::vector<float> weight (acc[0].size(), 0.0f);
        // Solo se guarda el trozo actual (unos 20 MB), nunca la cancion entera en frecuencia.
        std::vector<std::complex<float>> spec ((size_t) 2 * segmentFrames * bins);
        std::vector<float> mask ((size_t) 2 * segmentFrames * modelBins);
        std::vector<float> buffer ((size_t) nfft * 2);

        // Muestra del canal 'ch' en la posicion 'i' de la senal con relleno reflejado.
        const auto sampleAt = [&] (int ch, int i)
        {
            i -= pad;
            if (n == 1) return in.getSample (ch, 0);
            while (i < 0 || i >= n)
                i = i < 0 ? -i : 2 * (n - 1) - i;
            return in.getSample (ch, i);
        };

        for (int s = 0; s < segments; ++s)
        {
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < segmentFrames; ++f)
                {
                    const int frame = s * segmentFrames + f;
                    auto* row = &spec[((size_t) c * segmentFrames + (size_t) f) * (size_t) bins];
                    if (frame >= frames)
                    {
                        std::fill (row, row + bins, std::complex<float>());
                        continue;
                    }
                    std::fill (buffer.begin(), buffer.end(), 0.0f);
                    for (int i = 0; i < nfft; ++i)
                        buffer[(size_t) i] = sampleAt (juce::jmin (c, in.getNumChannels() - 1), frame * hop + i) * window[(size_t) i];
                    fft->performRealOnlyForwardTransform (buffer.data(), true);
                    for (int k = 0; k < bins; ++k)
                        row[k] = { buffer[(size_t) 2 * k], buffer[(size_t) 2 * k + 1] };
                }

            std::fill (mask.begin(), mask.end(), 0.0f);
            if (! maskForSegment (s, spec, bins, mask))
                return false;

            for (int f = 0; f < segmentFrames; ++f)
            {
                const int frame = s * segmentFrames + f;
                if (frame >= frames)
                    break;
                const int at = frame * hop;
                for (int i = 0; i < nfft; ++i)
                    weight[(size_t) (at + i)] += window[(size_t) i] * window[(size_t) i];
                for (int c = 0; c < 2; ++c)
                {
                    const auto* row = &spec[((size_t) c * segmentFrames + (size_t) f) * (size_t) bins];
                    const auto* m = &mask[((size_t) c * segmentFrames + (size_t) f) * (size_t) modelBins];
                    std::fill (buffer.begin(), buffer.end(), 0.0f);
                    for (int k = 0; k < juce::jmin (bins, modelBins); ++k)   // por encima: mascara 0
                    {
                        buffer[(size_t) 2 * k] = row[k].real() * m[k];
                        buffer[(size_t) 2 * k + 1] = row[k].imag() * m[k];
                    }
                    fft->performRealOnlyInverseTransform (buffer.data());
                    auto& a = acc[(size_t) c];
                    for (int i = 0; i < nfft; ++i)
                        a[(size_t) (at + i)] += buffer[(size_t) i] * window[(size_t) i];
                }
            }
            if (progress && ! progress ((double) (s + 1) / segments))
                return false;
        }

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float w = weight[(size_t) (i + pad)];
                out.setSample (c, i, w > 1e-6f ? acc[(size_t) c][(size_t) (i + pad)] / w : 0.0f);
            }
        return true;
    }
};
StemSeparator::StemSeparator() : impl (std::make_unique<Impl>()) {}
StemSeparator::~StemSeparator() = default;

bool StemSeparator::isLoaded() const
{
    return impl->vocals != nullptr && impl->accompaniment != nullptr;
}

juce::String StemSeparator::getLastError() const
{
    return impl->lastError;
}

juce::String StemSeparator::getMetadata() const
{
    return impl->metadata;
}

juce::String StemSeparator::describe() const
{
    return impl->modelName + " en " + impl->deviceName + " (STFT " + juce::String (impl->nfft) + "/" + juce::String (impl->hop)
           + (impl->center ? ", centrada)" : ")");
}

bool StemSeparator::load (const juce::File& folder, Device device, juce::String& error)
{
    auto find = [&folder] (const char* prefix)
    {
        const auto files = folder.findChildFiles (juce::File::findFiles, false, juce::String (prefix) + "*.onnx");
        return files.isEmpty() ? juce::File() : files[0];
    };
    const auto vocalsFile = find ("vocals"), accFile = find ("accompaniment");
    if (! vocalsFile.existsAsFile() || ! accFile.existsAsFile())
    {
        error = "No se encuentran vocals*.onnx y accompaniment*.onnx en " + folder.getFullPathName();
        return false;
    }

    try
    {
        Ort::SessionOptions options;
        // Deja un nucleo libre para el audio y la interfaz.
        options.SetIntraOpNumThreads ((int) juce::jmax (1u, std::thread::hardware_concurrency() - 1));
        options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
        impl->deviceName = "CPU";
        if (device == Device::gpu)
        {
            options.DisableMemPattern();
            options.SetExecutionMode (ExecutionMode::ORT_SEQUENTIAL);
            Ort::ThrowOnError (OrtSessionOptionsAppendExecutionProvider_DML (options, 0));
            impl->deviceName = "GPU (DirectML)";
        }
        impl->vocals = std::make_unique<Ort::Session> (impl->env, vocalsFile.getFullPathName().toWideCharPointer(), options);
        impl->accompaniment = std::make_unique<Ort::Session> (impl->env, accFile.getFullPathName().toWideCharPointer(), options);

        Ort::AllocatorWithDefaultOptions allocator;
        impl->vocalsIn = impl->vocals->GetInputNameAllocated (0, allocator).get();
        impl->vocalsOut = impl->vocals->GetOutputNameAllocated (0, allocator).get();
        impl->accIn = impl->accompaniment->GetInputNameAllocated (0, allocator).get();
        impl->accOut = impl->accompaniment->GetOutputNameAllocated (0, allocator).get();

        // Parametros de la STFT guardados en el modelo (si los trae).
        const auto meta = impl->vocals->GetModelMetadata();
        impl->metadata.clear();
        for (const auto& key : meta.GetCustomMetadataMapKeysAllocated (allocator))
            impl->metadata << key.get() << "=" << meta.LookupCustomMetadataMapAllocated (key.get(), allocator).get() << "; ";
        auto lookup = [&] (const char* key, const juce::String& fallback)
        {
            auto value = meta.LookupCustomMetadataMapAllocated (key, allocator);
            return value ? juce::String (value.get()) : fallback;
        };
        impl->nfft = lookup ("n_fft", "4096").getIntValue();
        impl->hop = lookup ("hop_length", "1024").getIntValue();
        impl->center = lookup ("center", "1") != "0" && lookup ("center", "1").toLowerCase() != "false";
        const auto rate = lookup ("sample_rate", "44100").getIntValue();
        // El Spleeter de sherpa-onnx dice sample_rate=41000: es una errata de la exportacion (todos los
        // modelos de Spleeter de Deezer son de 44,1 kHz y sus STFT de 4096/1024 son las de 44,1 kHz).
        const bool knownRate = rate == (int) modelSampleRate || rate == 41000;
        if (! knownRate || impl->nfft < 2 * modelBins || impl->hop <= 0)
        {
            error = "Modelo con parametros inesperados: " + juce::String (rate) + " Hz, n_fft " + juce::String (impl->nfft);
            impl->vocals.reset();
            impl->accompaniment.reset();
            return false;
        }
        impl->modelName = folder.getFileName();
        impl->prepareStft();
        return true;
    }
    catch (const Ort::Exception& e)
    {
        error = juce::String ("ONNX Runtime: ") + e.what();
        impl->vocals.reset();
        impl->accompaniment.reset();
        return false;
    }
}

bool StemSeparator::separateVocals (const juce::AudioBuffer<float>& stereo, juce::AudioBuffer<float>& out,
                                    const std::function<bool (double)>& progress)
{
    if (! isLoaded())
        return false;
    auto& m = *impl;
    std::vector<float> input ((size_t) 2 * segmentFrames * modelBins);
    const auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
    const std::array<int64_t, 4> shape { 2, 1, segmentFrames, modelBins };

    try
    {
        return m.process (stereo, out, [&] (int, const std::vector<std::complex<float>>& spec, int bins, std::vector<float>& mask)
        {
            // Entrada: magnitud de los 1024 primeros bins, (canal, trozo, fotograma, bin).
            for (int c = 0; c < 2; ++c)
                for (int f = 0; f < segmentFrames; ++f)
                    for (int k = 0; k < modelBins; ++k)
                        input[((size_t) c * segmentFrames + (size_t) f) * modelBins + (size_t) k]
                            = std::abs (spec[((size_t) c * segmentFrames + (size_t) f) * (size_t) bins + (size_t) k]);

            auto run = [&] (Ort::Session& session, const std::string& inName, const std::string& outName)
            {
                auto tensor = Ort::Value::CreateTensor<float> (memory, input.data(), input.size(), shape.data(), shape.size());
                const char* inNames[] = { inName.c_str() };
                const char* outNames[] = { outName.c_str() };
                return session.Run (Ort::RunOptions { nullptr }, inNames, &tensor, 1, outNames, 1);
            };
            auto v = run (*m.vocals, m.vocalsIn, m.vocalsOut);
            auto a = run (*m.accompaniment, m.accIn, m.accOut);
            const float* pv = v[0].GetTensorData<float>();
            const float* pa = a[0].GetTensorData<float>();
            if (v[0].GetTensorTypeAndShapeInfo().GetElementCount() < input.size()
                || a[0].GetTensorTypeAndShapeInfo().GetElementCount() < input.size())
                return false;
            // Mascara de voz de potencias (la de sherpa-onnx): (v^2 + e/2) / (v^2 + a^2 + e).
            for (size_t i = 0; i < input.size(); ++i)
                mask[i] = (pv[i] * pv[i] + 5e-11f) / (pv[i] * pv[i] + pa[i] * pa[i] + 1e-10f);
            return true;
        }, progress);
    }
    catch (const Ort::Exception& e)
    {
        impl->lastError = juce::String ("ONNX Runtime: ") + e.what();
        return false;
    }
}

void StemSeparator::separateWithMask (const juce::AudioBuffer<float>& stereo, juce::AudioBuffer<float>& out,
                                      const std::function<float (int, int)>& maskFn)
{
    if (impl->fft == nullptr)
        impl->prepareStft();
    impl->process (stereo, out, [&] (int segment, const std::vector<std::complex<float>>&, int, std::vector<float>& mask)
    {
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < segmentFrames; ++f)
                for (int k = 0; k < modelBins; ++k)
                    mask[((size_t) c * segmentFrames + (size_t) f) * modelBins + (size_t) k] = maskFn (k, segment * segmentFrames + f);
        return true;
    }, {});
}

juce::AudioBuffer<float> StemSeparator::resample (const juce::AudioBuffer<float>& in, double fromRate, double toRate)
{
    if (std::abs (fromRate - toRate) < 1e-6)
        return in;
    const double ratio = fromRate / toRate;
    const int outLength = (int) std::floor (in.getNumSamples() / ratio);
    juce::AudioBuffer<float> out (in.getNumChannels(), outLength);
    for (int ch = 0; ch < in.getNumChannels(); ++ch)
    {
        juce::WindowedSincInterpolator interpolator;
        interpolator.process (ratio, in.getReadPointer (ch), out.getWritePointer (ch), outLength,
                              in.getNumSamples(), 0);
    }
    return out;
}
}
