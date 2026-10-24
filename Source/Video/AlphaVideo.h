#pragma once

#include <juce_core/juce_core.h>

#include <functional>

namespace midj
{
// Videos con transparencia (WebM VP9 con alfa, MOV ProRes 4444 o PNG...): Media Foundation no
// lee su transparencia, asi que al elegirlos se convierten con FFmpeg a un MP4 "con mate" (el
// color a la izquierda y la transparencia en grises a la derecha), lo mismo que hace
// Herramientas\alpha-a-mate.ps1. FFmpeg no va con MiDJ (pesa mucho): se busca en el equipo y,
// si no esta, se puede descargar a la carpeta de datos.
namespace AlphaVideo
{
    // .webm, .mov o .mkv: los que pueden traer transparencia.
    bool mightHaveAlpha (const juce::File& video);

    // FFmpeg: el descargado (%APPDATA%\MiDJ\ffmpeg), junto a MiDJ, en el PATH o en D:\Tools\ffmpeg\bin.
    // juce::File() si no hay.
    juce::File findFfmpeg();
    juce::File downloadedFfmpeg();              // donde queda el descargado
    extern const char* const ffmpegDownloadUrl; // la compilacion GPL de BtbN en GitHub (unos 200 MB)

    // Lo que FFmpeg dice del archivo (ffmpeg -i): si su video tiene transparencia y la duracion.
    struct Info
    {
        bool alpha = false;
        double seconds = 0.0;
        int width = 0, height = 0;
    };
    Info parseInfo (const juce::String& ffmpegOutput);
    // Bloquea (unos 100 ms). false si FFmpeg no arranca o no entiende el archivo.
    bool probe (const juce::File& ffmpeg, const juce::File& video, Info& info);

    // Donde queda el convertido: <nombre>_mate.mp4 junto al original (si no se puede escribir alli,
    // en %APPDATA%\MiDJ\Mate). Si ya existe, se usa sin convertir otra vez.
    juce::File matteFileFor (const juce::File& video);

    // Los argumentos de FFmpeg: cada mitad cabe en maxWidth x maxHeight (el programa) sin
    // deformar ni agrandar, H.264 y el sonido en AAC. Avisa del avance por la salida (-progress).
    juce::StringArray convertArguments (const juce::File& ffmpeg, const juce::File& video, const juce::File& output,
                                        int maxWidth, int maxHeight);
    // Bloquea: llamar desde un hilo aparte. progress (0..1) devuelve false para cancelar. Escribe
    // en un temporal y lo renombra al terminar (cancelado o con error no queda nada a medias).
    bool convert (const juce::File& ffmpeg, const juce::File& video, const juce::File& output, int maxWidth, int maxHeight,
                  double seconds, const std::function<bool (double)>& progress, juce::String& error);
}
}
