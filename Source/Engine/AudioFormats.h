#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace midj
{
// El MP3 de JUCE con la duracion bien en los MPEG-2 y 2.5 (22 050, 24 000, 16 000 Hz...): JUCE
// cuenta 1152 muestras por trama, pero en Layer III de esas versiones son 576, y la cancion salia
// el doble de larga (la segunda mitad, silencio). El audio que decodifica es correcto.
// Limite: un salto (leer desde otra posicion que la siguiente) sigue cayendo al doble de lejos en
// esos archivos; los decks, la biblioteca y la forma de onda los leen de principio a fin.
class Mp3Format : public juce::MP3AudioFormat
{
public:
    using juce::MP3AudioFormat::createReaderFor;
    juce::AudioFormatReader* createReaderFor (juce::InputStream* stream, bool deleteStreamIfOpeningFails) override;

    // Muestras por trama de un MP3 segun su primera trama (tras la etiqueta ID3v2): 1152 en
    // MPEG-1 (y Layer I/II), 576 en Layer III de MPEG-2/2.5; 0 si no se reconoce.
    static int samplesPerFrame (juce::InputStream& stream);
};

// Como registerBasicFormats (WAV, AIFF, FLAC, Ogg y MP3), con el MP3 de arriba.
void registerAudioFormats (juce::AudioFormatManager& formats);
}
