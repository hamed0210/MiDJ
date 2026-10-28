#include "Engine/AudioFormats.h"

namespace midj
{
int Mp3Format::samplesPerFrame (juce::InputStream& stream)
{
    const auto start = stream.getPosition();
    int result = 0;
    // Etiqueta ID3v2 delante (tamano "syncsafe": 7 bits por byte; mas 10 si lleva pie).
    juce::int64 audioStart = start;
    juce::uint8 tag[10] = {};
    if (stream.read (tag, 10) == 10 && tag[0] == 'I' && tag[1] == 'D' && tag[2] == '3')
        audioStart += 10 + ((tag[6] & 0x7f) << 21 | (tag[7] & 0x7f) << 14 | (tag[8] & 0x7f) << 7 | (tag[9] & 0x7f))
                      + ((tag[5] & 0x10) != 0 ? 10 : 0);
    // La primera cabecera de trama valida en los primeros 64 KB.
    juce::HeapBlock<juce::uint8> data (65536);
    stream.setPosition (audioStart);
    const int n = stream.read (data, 65536);
    for (int i = 0; i + 3 < n; ++i)
    {
        const int b1 = data[i + 1], b2 = data[i + 2];
        const int version = (b1 >> 3) & 3, layer = (b1 >> 1) & 3;   // version: 3 MPEG-1, 2 MPEG-2, 0 MPEG-2.5
        if (data[i] != 0xff || (b1 & 0xe0) != 0xe0 || version == 1 || layer == 0 || (b2 >> 4) == 0xf || ((b2 >> 2) & 3) == 3)
            continue;
        result = layer == 1 && version != 3 ? 576 : 1152;   // layer 1 = Layer III
        break;
    }
    stream.setPosition (start);
    return result;
}

juce::AudioFormatReader* Mp3Format::createReaderFor (juce::InputStream* stream, bool deleteStreamIfOpeningFails)
{
    const int perFrame = stream != nullptr ? samplesPerFrame (*stream) : 0;
    auto* reader = juce::MP3AudioFormat::createReaderFor (stream, deleteStreamIfOpeningFails);
    // Solo si JUCE tambien vio una frecuencia de MPEG-2/2.5 (por debajo de 32 kHz): una falsa
    // cabecera al principio de un MP3 normal no lo acorta.
    if (reader != nullptr && perFrame == 576 && reader->sampleRate < 32000.0)
        reader->lengthInSamples /= 2;   // JUCE conto 1152 muestras por trama
    return reader;
}

void registerAudioFormats (juce::AudioFormatManager& formats)
{
    formats.registerFormat (new juce::WavAudioFormat(), true);
    formats.registerFormat (new juce::AiffAudioFormat(), false);
   #if JUCE_USE_FLAC
    formats.registerFormat (new juce::FlacAudioFormat(), false);
   #endif
   #if JUCE_USE_OGGVORBIS
    formats.registerFormat (new juce::OggVorbisAudioFormat(), false);
   #endif
    formats.registerFormat (new Mp3Format(), false);
}
}
