#pragma once

#include "Engine/BpmDetector.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <functional>
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

    // Quitar de la biblioteca una carpeta del arbol (un disco, una carpeta dentro de otra de la
    // biblioteca...). No borra archivos ni pierde el analisis: sus canciones quedan ocultas y no
    // vuelven al re-escanear. Devuelve lo necesario para deshacerlo.
    struct Removal
    {
        juce::File folder;
        juce::Array<juce::File> roots;   // carpetas de la biblioteca que estaban dentro (o era ella)
    };
    Removal removeFromLibrary (const juce::File& folder);
    void undoRemoval (const Removal& removal);
    // Rutas de todas las canciones (para el arbol de carpetas).
    juce::StringArray getTrackPaths() const;

    // Sincronizar con el PC: recorre las carpetas (sin las del sistema) y devuelve las que tienen
    // musica o video, sin repetir las que ya estan dentro de otra encontrada. progress recibe la
    // carpeta que se mira y los archivos encontrados; shouldStop corta la busqueda.
    struct SyncOptions
    {
        juce::Array<juce::File> roots;   // discos o carpetas donde buscar
        bool audio = true, video = true;
    };
    static juce::Array<juce::File> findMediaFolders (const SyncOptions& options,
                                                     const std::function<void (const juce::File&, int)>& progress,
                                                     const std::function<bool()>& shouldStop, int& filesFound);
    // Discos fijos del equipo (C:, D:...).
    static juce::Array<juce::File> localDrives();
    // Nombre de un disco como en el Explorador: "Datos (D:)" o "Disco local (C:)".
    static juce::String driveName (const juce::File& drive);
    static bool isVideoFile (const juce::File& file);

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
    juce::StringArray getExclusions() const;   // carpetas quitadas (con la barra final)
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
