#include "Engine/SampleData.h"

#include <cmath>
#include <cstring>

namespace midj
{
namespace
{
    template <typename T>
    T readLE (const juce::MemoryBlock& d, size_t offset)
    {
        T v {};
        if (offset + sizeof (T) <= d.getSize())
            std::memcpy (&v, static_cast<const char*> (d.getData()) + offset, sizeof (T));   // Windows es little-endian
        return v;
    }

    // Decodifica [from, from + length) segundos (length <= 0 = hasta el final) a estereo.
    std::shared_ptr<SampleData> decode (std::unique_ptr<juce::AudioFormatReader> reader, double from, double length,
                                        double maxSeconds)
    {
        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            return nullptr;
        const double rate = reader->sampleRate;
        const auto total = reader->lengthInSamples;
        const auto start = juce::jlimit<juce::int64> (0, total, (juce::int64) std::llround (from * rate));
        auto count = length > 0.0 ? (juce::int64) std::llround (length * rate) : total - start;
        count = juce::jlimit<juce::int64> (0, juce::jmin (total - start, (juce::int64) (maxSeconds * rate)), count);
        if (count <= 0)
            return nullptr;

        auto s = std::make_shared<SampleData>();
        s->sampleRate = rate;
        s->audio.setSize (2, (int) count);
        reader->read (&s->audio, 0, (int) count, start, true, true);
        return s;
    }
}

int SampleData::lengthBeats() const
{
    if (bpm <= 0.0)
        return 0;
    return juce::jmax (1, (int) std::lround (lengthSeconds() * bpm / 60.0));
}

std::shared_ptr<SampleData> SampleData::loadFile (const juce::File& file, juce::AudioFormatManager& formats, double maxSeconds)
{
    if (file.hasFileExtension ("vdjsample"))
    {
        juce::MemoryBlock data;
        if (! file.loadFileAsData (data))
            return nullptr;
        auto s = loadVdjSample (data, file.getFileNameWithoutExtension(), formats);
        if (s != nullptr)
            s->source = file;
        return s;
    }
    auto s = decode (std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file)), 0.0, 0.0, maxSeconds);
    if (s != nullptr)
    {
        s->name = file.getFileNameWithoutExtension();
        s->source = file;
    }
    return s;
}

std::shared_ptr<SampleData> SampleData::loadVdjSample (const juce::MemoryBlock& data, const juce::String& name,
                                                       juce::AudioFormatManager& formats)
{
    if (data.getSize() < 0x70 || std::memcmp (data.getData(), "VDJ", 4) != 0)
        return nullptr;
    const auto headerSize = readLE<uint32_t> (data, 0x08);
    const auto audioSize = readLE<uint32_t> (data, 0x0C);
    if (headerSize < 0x50 || (size_t) headerSize + audioSize > data.getSize() || audioSize == 0)
        return nullptr;

    // El audio va tal cual dentro del archivo (Ogg Vorbis en los de VirtualDJ). Los streams
    // apuntan a 'data' sin copiarla: se decodifica entero antes de salir de esta funcion.
    const char* audioStart = static_cast<const char*> (data.getData()) + headerSize;
    std::unique_ptr<juce::AudioFormatReader> reader;
    if (auto* ogg = formats.findFormatForFileExtension ("ogg"))
        reader.reset (ogg->createReaderFor (new juce::MemoryInputStream (audioStart, audioSize, false), true));
    if (reader == nullptr)
        reader.reset (formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (audioStart, audioSize, false)));
    const double from = readLE<double> (data, 0x28), length = readLE<double> (data, 0x30);
    auto s = decode (std::move (reader), std::isfinite (from) ? from : 0.0, std::isfinite (length) ? length : 0.0, 60.0);
    if (s == nullptr)
        return nullptr;

    s->name = name;
    s->loop = readLE<uint8_t> (data, 0x18) != 0;
    const auto loopMode = readLE<uint32_t> (data, 0x1C);
    s->tempoMode = loopMode == 3 ? 2 : loopMode >= 1 ? 1 : 0;   // locked -> bloqueado; pitched/sync -> sincronizado
    const float secondsPerBeat = readLE<float> (data, 0x20);
    s->bpm = secondsPerBeat > 0.05f && secondsPerBeat < 5.0f ? 60.0 / secondsPerBeat : 0.0;
    // La fase es del audio entero: se pasa al fragmento recortado.
    const double phase = readLE<float> (data, 0x24);
    if (s->bpm > 0.0)
    {
        const double period = 60.0 / s->bpm;
        s->firstBeat = std::fmod (std::fmod (phase - from, period) + period, period);
        if (s->firstBeat > period - 1e-3)
            s->firstBeat = 0.0;
    }
    const float gain = readLE<float> (data, 0x48);
    s->gain = gain > 0.0f && gain < 16.0f ? gain : 1.0f;
    return s;
}
}
