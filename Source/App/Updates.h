#pragma once

#include <juce_core/juce_core.h>

namespace midj
{
// Actualizaciones: las versiones de MiDJ se publican en GitHub (Releases) con el instalador
// MiDJ-Setup-<version>.exe. Se pregunta a la API de GitHub por la ultima (sin clave; el limite
// es de 60 consultas por hora y equipo) y se compara con la que corre.
namespace Updates
{
    constexpr const char* repository = "hamed0210/MiDJ";
    juce::String releasesPage();   // https://github.com/hamed0210/MiDJ/releases/latest

    struct Release
    {
        juce::String version;        // "0.4.0" (de la etiqueta v0.4.0)
        juce::String name, notes;    // titulo y novedades (Markdown, como se escribio en GitHub)
        juce::String pageUrl;        // la pagina de la version en GitHub
        juce::String installerUrl;   // el .exe del instalador ("" si la version no lo trae)
        juce::int64 installerSize = 0;
    };

    // Negativo si a es anterior a b, 0 si son la misma, positivo si es posterior ("0.3.10" > "0.3.9").
    // Acepta la "v" delante y partes de distinta longitud ("1.0" == "1.0.0").
    int compareVersions (const juce::String& a, const juce::String& b);

    // La respuesta de /repos/<repo>/releases/latest. false si no es una version valida.
    bool parseLatestRelease (const juce::String& json, Release& out);

    // Las novedades en texto simple para un dialogo: sin #, ** ni `, vinetas con un punto medio.
    juce::String plainNotes (const juce::String& markdown);

    // Bloquea (red): llamar desde un hilo aparte. false con error si no se pudo consultar.
    bool fetchLatest (Release& out, juce::String& error);

    // Ajustes en %APPDATA%\MiDJ\updates.xml: buscar al abrir (una vez al dia como mucho), la
    // version que el usuario dijo que no queria y cuando se miro por ultima vez.
    struct Settings
    {
        bool checkOnStartup = true;
        juce::String skippedVersion;
        juce::int64 lastCheckMs = 0;

        static Settings load (const juce::File& file);
        void save (const juce::File& file) const;
        bool dueForCheck (juce::int64 nowMs) const { return checkOnStartup && nowMs - lastCheckMs > 20LL * 3600 * 1000; }
    };
}
}
