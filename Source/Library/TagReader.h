#pragma once

#include <juce_core/juce_core.h>

namespace midj
{
// Datos de una cancion que vienen del archivo (no del analisis).
struct TrackTags
{
    juce::String title, artist, album, genre;
    double bpm = 0.0;   // si el archivo trae BPM en sus etiquetas (solo informativo)
};

// Lee las etiquetas de un archivo de audio:
//  - MP3: ID3v2 (versiones 2.2, 2.3 y 2.4) al principio o, si no hay, ID3v1 al final.
//  - Si falta el titulo o el artista se sacan del nombre "Artista - Titulo.mp3".
namespace TagReader
{
    TrackTags read (const juce::File& file);

    // Partes sueltas, publicas para las pruebas.
    TrackTags parseId3v2 (const void* data, size_t size);       // data empieza por "ID3"
    TrackTags parseId3v1 (const void* data, size_t size);       // los 128 bytes finales, "TAG..."
    void fillFromFileName (TrackTags& tags, const juce::String& fileNameWithoutExtension);
}
}
