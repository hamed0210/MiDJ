#pragma once

#include "Engine/BpmDetector.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <optional>
#include <vector>

struct sqlite3;

namespace midj
{
struct TrackInfo
{
    juce::int64 id = 0;
    juce::String path, title, artist, album, genre;
    double duration = 0.0;
    double bpm = 0.0, firstBeat = 0.0;   // bpm 0 = aun sin analizar
    bool analysed = false;

    juce::File getFile() const { return juce::File (path); }
};

// Biblioteca de canciones en SQLite, como el database.xml de VirtualDJ: guarda las etiquetas y
// el analisis de tempo de cada archivo para no repetirlo al cargarlo en un deck.
//
// Un hilo de fondo (prioridad baja) recorre las carpetas añadidas, da de alta los archivos
// nuevos o cambiados, quita los que ya no existen y despues analiza el BPM de los pendientes
// de uno en uno. Avisa con sendChangeMessage() cuando hay datos nuevos.
// Todos los metodos se pueden llamar desde cualquier hilo.
class Library : public juce::ChangeBroadcaster
{
public:
    enum class SortColumn { title, artist, bpm, duration, album, genre, added };

    explicit Library (juce::AudioFormatManager& formats);
    ~Library() override;

    // Abre (o crea) la base de datos. Por defecto en %APPDATA%\MiDJ\library.db.
    bool open (const juce::File& databaseFile = getDefaultFile());
    void close();
    static juce::File getDefaultFile();

    // Carpetas de musica.
    void addFolder (const juce::File& folder);   // solo la apunta: despues, startBackgroundWork()
    void removeFolder (const juce::File& folder);
    juce::Array<juce::File> getFolders() const;

    // Trabajo de fondo: escanear todas las carpetas y analizar lo pendiente.
    void startBackgroundWork();
    bool isBusy() const;
    juce::String getStatusText() const;   // p. ej. "Analizando 2 de 15"

    // Lo mismo pero en el hilo que llama (pruebas y herramientas).
    void scanNow();
    int analysePendingNow (int maxTracks = 1 << 30);

    // Consultas. 'text': palabras que deben aparecer todas (sin importar acentos ni mayusculas)
    // en titulo, artista, album, genero o nombre de archivo. 'folder' vacio = toda la musica.
    std::vector<TrackInfo> search (const juce::String& text, const juce::File& folder = {},
                                   SortColumn sort = SortColumn::artist, bool ascending = true) const;
    std::optional<TrackInfo> findByFile (const juce::File& file) const;
    int getNumTracks() const;

    // Guarda el analisis hecho fuera (por ejemplo al cargar un archivo en un deck).
    void setAnalysis (const juce::File& file, const TempoAnalysis& analysis);

    // Minusculas y sin acentos: "María Ñandú" -> "maria nandu".
    static juce::String normalise (const juce::String& text);
    // Extensiones de audio que entran en la biblioteca.
    static bool isAudioFile (const juce::File& file);

private:
    class Worker;
    bool exec (const char* sql) const;
    void scanFolder (const juce::File& folder, juce::Thread* thread);   // thread: para poder parar
    bool analyseOne (juce::Thread* thread);                              // false = no quedaba nada
    void startPendingCount();

    juce::AudioFormatManager& formats;
    sqlite3* db = nullptr;
    juce::CriticalSection lock;          // una sola conexion compartida por todos los hilos
    std::unique_ptr<Worker> worker;
    std::atomic<int> pendingTotal { 0 }, pendingDone { 0 };
    std::atomic<bool> scanning { false };
};
}
