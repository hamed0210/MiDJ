#include "Library/TagReader.h"

namespace midj::TagReader
{
namespace
{
    constexpr size_t maxTagBytes = 4 * 1024 * 1024;   // las caratulas pueden ser grandes; el texto va antes

    uint32_t bigEndian (const uint8_t* p, int bytes)
    {
        uint32_t v = 0;
        for (int i = 0; i < bytes; ++i)
            v = (v << 8) | p[i];
        return v;
    }

    // Tamano "syncsafe" de ID3: 4 bytes de 7 bits utiles.
    uint32_t syncSafe (const uint8_t* p)
    {
        return ((uint32_t) (p[0] & 0x7f) << 21) | ((uint32_t) (p[1] & 0x7f) << 14)
             | ((uint32_t) (p[2] & 0x7f) << 7) | (uint32_t) (p[3] & 0x7f);
    }

    juce::String latin1 (const uint8_t* p, size_t n)
    {
        juce::String s;
        for (size_t i = 0; i < n && p[i] != 0; ++i)
            s += (juce::juce_wchar) p[i];   // ISO-8859-1 coincide con los primeros 256 de Unicode
        return s;
    }

    juce::String utf16 (const uint8_t* p, size_t n, bool bigEndianOrder)
    {
        if (n >= 2 && ((p[0] == 0xff && p[1] == 0xfe) || (p[0] == 0xfe && p[1] == 0xff)))
        {
            bigEndianOrder = p[0] == 0xfe;
            p += 2;
            n -= 2;
        }
        juce::String s;
        for (size_t i = 0; i + 1 < n; i += 2)
        {
            juce::juce_wchar c = bigEndianOrder ? (juce::juce_wchar) ((p[i] << 8) | p[i + 1])
                                                : (juce::juce_wchar) ((p[i + 1] << 8) | p[i]);
            if (c == 0)
                break;
            // Pares sustitutos (caracteres fuera del plano basico, p. ej. emojis).
            if (c >= 0xd800 && c < 0xdc00 && i + 3 < n)
            {
                const juce::juce_wchar low = bigEndianOrder ? (juce::juce_wchar) ((p[i + 2] << 8) | p[i + 3])
                                                            : (juce::juce_wchar) ((p[i + 3] << 8) | p[i + 2]);
                c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
                i += 2;
            }
            s += c;
        }
        return s;
    }

    // Texto de un marco ID3: primer byte = codificacion; puede traer varios valores separados
    // por ceros (2.4): se usa el primero.
    juce::String frameText (const uint8_t* p, size_t n)
    {
        if (n < 1)
            return {};
        const uint8_t encoding = p[0];
        ++p;
        --n;
        switch (encoding)
        {
            case 1:  return utf16 (p, n, false).trim();
            case 2:  return utf16 (p, n, true).trim();
            case 3:  return juce::String::fromUTF8 ((const char*) p, (int) strnlen ((const char*) p, n)).trim();
            default: return latin1 (p, n).trim();
        }
    }

    // Genero al estilo ID3v1 "(17)" o "(17)Rock": se quita la referencia numerica.
    juce::String cleanGenre (juce::String g)
    {
        while (g.startsWithChar ('(') && g.containsChar (')'))
        {
            const auto inside = g.fromFirstOccurrenceOf ("(", false, false).upToFirstOccurrenceOf (")", false, false);
            if (! inside.containsOnly ("0123456789") && inside != "RX" && inside != "CR")
                break;
            g = g.fromFirstOccurrenceOf (")", false, false).trim();
        }
        return g.containsOnly ("0123456789") ? juce::String() : g;
    }
}

TrackTags parseId3v2 (const void* data, size_t size)
{
    TrackTags tags;
    auto* d = static_cast<const uint8_t*> (data);
    if (size < 10 || d[0] != 'I' || d[1] != 'D' || d[2] != '3')
        return tags;

    const int version = d[3];   // 2, 3 o 4
    const uint8_t flags = d[5];
    if (version < 2 || version > 4)
        return tags;
    const size_t tagSize = std::min ((size_t) syncSafe (d + 6) + 10, size);

    // Desincronizacion de todo el tag (2.2 y 2.3): cada FF 00 vale FF.
    juce::MemoryBlock clean;
    if ((flags & 0x80) != 0 && version < 4)
    {
        clean.ensureSize (tagSize);
        size_t out = 0;
        for (size_t i = 0; i < tagSize; ++i)
        {
            clean[out++] = (char) d[i];
            if (d[i] == 0xff && i + 1 < tagSize && d[i + 1] == 0x00)
                ++i;
        }
        clean.setSize (out);
        d = static_cast<const uint8_t*> (clean.getData());
    }
    const size_t end = (flags & 0x80) != 0 && version < 4 ? clean.getSize() : tagSize;

    size_t pos = 10;
    if ((flags & 0x40) != 0 && version >= 3 && pos + 4 <= end)   // cabecera extendida
        pos += version == 4 ? syncSafe (d + pos) : bigEndian (d + pos, 4) + 4;

    const size_t idLength = version == 2 ? 3 : 4, headerLength = version == 2 ? 6 : 10;
    while (pos + headerLength <= end && d[pos] != 0)
    {
        const juce::String id ((const char*) d + pos, idLength);
        const size_t frameSize = version == 2 ? bigEndian (d + pos + 3, 3)
                               : version == 4 ? syncSafe (d + pos + 4) : bigEndian (d + pos + 4, 4);
        const size_t body = pos + headerLength;
        if (frameSize == 0 || body + frameSize > end)
            break;

        const auto text = [&] { return frameText (d + body, frameSize); };
        if      (id == "TIT2" || id == "TT2") tags.title = text();
        else if (id == "TPE1" || id == "TP1") tags.artist = text();
        else if (id == "TALB" || id == "TAL") tags.album = text();
        else if (id == "TCON" || id == "TCO") tags.genre = cleanGenre (text());
        else if (id == "TBPM" || id == "TBP") tags.bpm = text().getDoubleValue();
        pos = body + frameSize;
    }
    return tags;
}

TrackTags parseId3v1 (const void* data, size_t size)
{
    TrackTags tags;
    auto* d = static_cast<const uint8_t*> (data);
    if (size < 128 || d[0] != 'T' || d[1] != 'A' || d[2] != 'G')
        return tags;
    tags.title = latin1 (d + 3, 30).trim();
    tags.artist = latin1 (d + 33, 30).trim();
    tags.album = latin1 (d + 63, 30).trim();
    return tags;
}

void fillFromFileName (TrackTags& tags, const juce::String& name)
{
    // "Artista - Titulo": se usa el primer " - " (los titulos pueden llevar mas guiones).
    const bool hasDash = name.contains (" - ");
    if (tags.artist.isEmpty() && hasDash)
        tags.artist = name.upToFirstOccurrenceOf (" - ", false, false).trim();
    if (tags.title.isEmpty())
        tags.title = hasDash ? name.fromFirstOccurrenceOf (" - ", false, false).trim() : name.trim();
}

TrackTags read (const juce::File& file)
{
    TrackTags tags;
    juce::FileInputStream in (file);
    if (in.openedOk())
    {
        uint8_t header[10] = {};
        if (in.read (header, 10) == 10 && header[0] == 'I' && header[1] == 'D' && header[2] == '3')
        {
            const size_t total = std::min ((size_t) syncSafe (header + 6) + 10, maxTagBytes);
            juce::MemoryBlock tag (total);
            std::memcpy (tag.getData(), header, 10);
            in.read (static_cast<char*> (tag.getData()) + 10, (int) total - 10);
            tags = parseId3v2 (tag.getData(), total);
        }
        else if (in.getTotalLength() >= 128 && in.setPosition (in.getTotalLength() - 128))
        {
            uint8_t v1[128] = {};
            if (in.read (v1, 128) == 128)
                tags = parseId3v1 (v1, 128);
        }
    }
    fillFromFileName (tags, file.getFileNameWithoutExtension());
    return tags;
}
}
