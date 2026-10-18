#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace midj
{
// Un video de YouTube (resultado de busqueda o elemento de la lista del automix de YouTube).
struct YouTubeVideo
{
    juce::String id;           // los 11 caracteres de youtube.com/watch?v=...
    juce::String title, channel;
    double seconds = 0.0;      // 0 = en directo o desconocida
};

// Busqueda en YouTube sin clave: se pide la pagina de resultados (como un navegador) y se leen
// los videos de los datos que trae (ytInitialData). Si YouTube cambia la pagina, solo hay que
// tocar parseResultsPage().
namespace YouTubeSearch
{
    // Bloquea (red): llamar desde un hilo aparte. error: por que no hubo resultados.
    std::vector<YouTubeVideo> search (const juce::String& query, juce::String& error);
    // La pagina de resultados -> videos (sin directos, cortos ni listas).
    std::vector<YouTubeVideo> parseResultsPage (const juce::String& html);
    // "3:41" o "1:02:03" -> segundos (0 si no se entiende).
    double parseDuration (const juce::String& text);
    // "3:41" (o "1:02:03"; "" si 0).
    juce::String formatDuration (double seconds);
    // Un enlace de YouTube o un id -> el id ("" si no lo es).
    juce::String idFromText (const juce::String& text);
}
}
