#include "Library/TagReader.h"

namespace midj::TagReader
{
namespace
{
    // Los 128 de arriba de la pagina 850 de DOS y los 32 de Windows-1252 que no son de Latin-1.
    constexpr juce::juce_wchar cp850[128] = {
        0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7, 0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
        0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9, 0x00ff, 0x00d6, 0x00dc, 0x00f8, 0x00a3, 0x00d8, 0x00d7, 0x0192,
        0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba, 0x00bf, 0x00ae, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
        0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x00c1, 0x00c2, 0x00c0, 0x00a9, 0x2563, 0x2551, 0x2557, 0x255d, 0x00a2, 0x00a5, 0x2510,
        0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x00e3, 0x00c3, 0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x00a4,
        0x00f0, 0x00d0, 0x00ca, 0x00cb, 0x00c8, 0x0131, 0x00cd, 0x00ce, 0x00cf, 0x2518, 0x250c, 0x2588, 0x2584, 0x00a6, 0x00cc, 0x2580,
        0x00d3, 0x00df, 0x00d4, 0x00d2, 0x00f5, 0x00d5, 0x00b5, 0x00fe, 0x00de, 0x00da, 0x00db, 0x00d9, 0x00fd, 0x00dd, 0x00af, 0x00b4,
        0x00ad, 0x00b1, 0x2017, 0x00be, 0x00b6, 0x00a7, 0x00f7, 0x00b8, 0x00b0, 0x00a8, 0x00b7, 0x00b9, 0x00b3, 0x00b2, 0x25a0, 0x00a0 };
    constexpr juce::juce_wchar cp1252[32] = {
        0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
        0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178 };

    // Pagina 850 de DOS (de programas viejos de Windows): ñ = A4, á = A0, é = 82... Se reconoce porque
    // esas letras van pegadas detras de otra letra ("Ma¤anita", "Caf‚"; en Windows-1252 serian
    // simbolos o comillas, que van delante de la palabra o entre espacios) y no hay letras de Latin-1
    // (C0-FF), que en la 850 serian dibujos de cajas. 'at (i)': el byte i (o el caracter, si es <= FF).
    template <typename At>
    bool looksDos (At at, size_t n)
    {
        auto isLetter = [] (unsigned c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
        auto isDosLetter = [] (unsigned c) { return c == 0x81 || c == 0x82 || c == 0x87 || c == 0x90 || (c >= 0xa0 && c <= 0xa5); };   // ü é ç É á í ó ú ñ Ñ
        bool latinLetters = false, dosLetters = false;
        for (size_t i = 0; i < n; ++i)
        {
            latinLetters = latinLetters || at (i) >= 0xc0;
            dosLetters = dosLetters || (isDosLetter (at (i)) && i > 0 && isLetter (at (i - 1)));
        }
        return dosLetters && ! latinLetters;
    }
}

juce::String repairDosText (const juce::String& text)
{
    // Texto ya leido (por ejemplo UTF-16) que trae la pagina 850 mal convertida: "Ma¤anita" -> "Mañanita".
    std::vector<unsigned> chars;
    for (auto c : text)
    {
        if ((unsigned) c > 0xff)
            return text;   // ya trae caracteres de fuera de Latin-1: no es este caso
        chars.push_back ((unsigned) c);
    }
    if (! looksDos ([&chars] (size_t i) { return chars[i]; }, chars.size()))
        return text;
    juce::String s;
    for (auto c : chars)
        s += c < 0x80 ? (juce::juce_wchar) c : cp850[c - 0x80];
    return s;
}

juce::String decode8bit (const void* data, size_t size)
{
    const auto* p = (const uint8_t*) data;
    size_t n = 0;
    bool high = false;
    for (; n < size && p[n] != 0; ++n)
        high = high || p[n] >= 0x80;
    if (! high)
        return juce::String (juce::CharPointer_ASCII ((const char*) p), n);
    // Hay programas que escriben UTF-8 aunque la etiqueta diga Latin-1 (se comprueba estricto: el
    // de JUCE da por bueno texto Latin-1 como "Ma\xf1ana").
    auto validUtf8 = [p, n]
    {
        for (size_t i = 0; i < n;)
        {
            const uint8_t c = p[i];
            const size_t extra = c < 0x80 ? 0 : (c >= 0xc2 && c <= 0xdf) ? 1 : (c >= 0xe0 && c <= 0xef) ? 2 : (c >= 0xf0 && c <= 0xf4) ? 3 : 99;
            if (extra == 99)
                return false;
            for (size_t k = 1; k <= extra; ++k)
                if (i + k >= n || (p[i + k] & 0xc0) != 0x80)
                    return false;
            i += extra + 1;
        }
        return true;
    };
    if (validUtf8())
        return juce::String::fromUTF8 ((const char*) p, (int) n);
    const bool dos = looksDos ([p] (size_t i) { return (unsigned) p[i]; }, n);
    juce::String s;
    s.preallocateBytes (n * 2);
    for (size_t i = 0; i < n; ++i)
    {
        const uint8_t c = p[i];
        s += c < 0x80 ? (juce::juce_wchar) c
                      : dos ? cp850[c - 0x80]
                            : (c < 0xa0 ? cp1252[c - 0x80] : (juce::juce_wchar) c);   // Windows-1252 (Latin-1 con comillas, euro...)
    }
    return s;
}

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
        return TagReader::decode8bit (p, n);
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
    for (auto* field : { &tags.title, &tags.artist, &tags.album, &tags.genre })
        *field = repairDosText (*field);
    fillFromFileName (tags, file.getFileNameWithoutExtension());
    return tags;
}
}
