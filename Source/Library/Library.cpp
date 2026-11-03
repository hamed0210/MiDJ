#include "Library/Library.h"
#include "Library/TagReader.h"
#include "Engine/AudioFormats.h"

#include <sqlite3.h>

#include <algorithm>
#include <map>
#include <set>

namespace midj
{
namespace
{
    constexpr double analysisSeconds = 120.0;   // igual que el deck: BpmDetector mira 2 minutos

    // Sentencia preparada de SQLite con liberacion automatica.
    class Statement
    {
    public:
        Statement (sqlite3* db, const char* sql)
        {
            if (db != nullptr && sqlite3_prepare_v2 (db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            {
                DBG ("SQLite: " << sqlite3_errmsg (db) << " en: " << sql);
                stmt = nullptr;
            }
        }
        ~Statement() { sqlite3_finalize (stmt); }

        bool ok() const { return stmt != nullptr; }
        Statement& bind (int i, const juce::String& s)
        {
            sqlite3_bind_text (stmt, i, s.toRawUTF8(), -1, SQLITE_TRANSIENT);
            return *this;
        }
        Statement& bind (int i, double v)      { sqlite3_bind_double (stmt, i, v); return *this; }
        Statement& bind (int i, juce::int64 v) { sqlite3_bind_int64 (stmt, i, v); return *this; }

        bool next() { return ok() && sqlite3_step (stmt) == SQLITE_ROW; }   // true = hay fila
        bool run()  { return ok() && sqlite3_step (stmt) == SQLITE_DONE; }

        juce::String text (int col) const
        {
            auto* p = sqlite3_column_text (stmt, col);
            return p != nullptr ? juce::String::fromUTF8 ((const char*) p) : juce::String();
        }
        double real (int col) const         { return sqlite3_column_double (stmt, col); }
        juce::int64 integer (int col) const { return sqlite3_column_int64 (stmt, col); }

    private:
        sqlite3_stmt* stmt = nullptr;
    };

    constexpr const char* schema = R"sql(
        PRAGMA journal_mode = WAL;
        PRAGMA synchronous = NORMAL;
        CREATE TABLE IF NOT EXISTS folders (
            id    INTEGER PRIMARY KEY,
            path  TEXT NOT NULL UNIQUE);
        CREATE TABLE IF NOT EXISTS tracks (
            id         INTEGER PRIMARY KEY,
            path       TEXT NOT NULL UNIQUE,
            title      TEXT, artist TEXT, album TEXT, genre TEXT,
            duration   REAL,
            bpm        REAL DEFAULT 0,
            first_beat REAL DEFAULT 0,
            analysed   INTEGER DEFAULT 0,      -- 0 pendiente, 1 hecho, -1 no se pudo leer
            file_size  INTEGER, file_mtime INTEGER,
            added      INTEGER,
            search     TEXT);                  -- todo el texto en minusculas y sin acentos
        DROP INDEX IF EXISTS tracks_pending;
        CREATE INDEX IF NOT EXISTS tracks_pending_added ON tracks (analysed, added);   -- la siguiente por analizar sin ordenar toda la tabla
        CREATE TABLE IF NOT EXISTS excluded (       -- carpetas quitadas del arbol (no se re-escanean)
            path  TEXT PRIMARY KEY);
        CREATE TABLE IF NOT EXISTS playlists (      -- listas de reproduccion propias
            id    INTEGER PRIMARY KEY,
            name  TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS playlist_items (
            playlist_id INTEGER NOT NULL,
            track_id    INTEGER NOT NULL,
            position    INTEGER NOT NULL,
            PRIMARY KEY (playlist_id, track_id));
        CREATE TABLE IF NOT EXISTS settings (       -- ajustes de la biblioteca (duraciones)
            key   TEXT PRIMARY KEY,
            value TEXT);
    )sql";

    constexpr const char* trackColumns = "id, path, title, artist, album, genre, duration, bpm, first_beat, analysed, favorite, plays";

    TrackInfo readTrack (const Statement& s)
    {
        TrackInfo t;
        t.id = s.integer (0);
        t.path = s.text (1);
        t.title = s.text (2);
        t.artist = s.text (3);
        t.album = s.text (4);
        t.genre = s.text (5);
        t.duration = s.real (6);
        t.bpm = s.real (7);
        t.firstBeat = s.real (8);
        t.analysed = s.integer (9) == 1;
        t.favorite = s.integer (10) != 0;
        t.plays = (int) s.integer (11);
        return t;
    }

    // Para LIKE: '!' es el caracter de escape.
    juce::String likePattern (const juce::String& word)
    {
        return "%" + word.replace ("!", "!!").replace ("%", "!%").replace ("_", "!_") + "%";
    }

    juce::String folderPrefix (const juce::File& folder)
    {
        auto p = folder.getFullPathName();
        return p.endsWithChar (juce::File::getSeparatorChar()) ? p : p + juce::File::getSeparatorChar();
    }
}

// Hilo de fondo: escanea cuando se le pide y analiza mientras haya pendientes.
class Library::Worker : public juce::Thread
{
public:
    explicit Worker (Library& l) : juce::Thread ("MiDJ biblioteca"), library (l) {}

    void requestScan()
    {
        scanRequested = true;
        notify();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            if (scanRequested.exchange (false))
            {
                library.scanning = true;
                library.contentChanged();
                library.fixMpeg2Durations (this);
                library.fixLegacyTagText (this);
                for (const auto& folder : library.getFolders())
                    if (! threadShouldExit())
                        library.scanFolder (folder, this);
                library.scanning = false;
                library.startPendingCount();
                library.contentChanged();
            }
            if (threadShouldExit())
                break;
            if (! library.analyseOne (this))
                wait (-1);   // nada pendiente: dormir hasta el proximo requestScan()
        }
    }

private:
    Library& library;
    std::atomic<bool> scanRequested { true };
};

Library::Library (juce::AudioFormatManager& f) : formats (f) {}

Library::~Library()
{
    close();
}

juce::File Library::getDefaultFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("MiDJ").getChildFile ("library.db");
}

bool Library::open (const juce::File& file)
{
    close();
    file.getParentDirectory().createDirectory();
    const juce::ScopedLock sl (lock);
    if (sqlite3_open_v2 (file.getFullPathName().toRawUTF8(), &db,
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
    {
        sqlite3_close (db);
        db = nullptr;
        return false;
    }
    sqlite3_busy_timeout (db, 2000);
    if (! exec (schema))
        return false;
    // Bases de versiones anteriores: columnas que se anadieron despues (canciones ocultas por
    // carpetas quitadas, favoritos y reproducciones).
    for (auto [name, definition] : { std::pair<const char*, const char*> { "hidden", "hidden INTEGER DEFAULT 0" },
                                     std::pair<const char*, const char*> { "favorite", "favorite INTEGER DEFAULT 0" },
                                     std::pair<const char*, const char*> { "plays", "plays INTEGER DEFAULT 0" },
                                     std::pair<const char*, const char*> { "last_played", "last_played INTEGER DEFAULT 0" } })
    {
        Statement check (db, "SELECT 1 FROM pragma_table_info ('tracks') WHERE name = ?");
        check.bind (1, juce::String (name));
        if (! check.next())
            exec ((juce::String ("ALTER TABLE tracks ADD COLUMN ") + definition).toRawUTF8());
    }
    // Duraciones: "minAudio maxAudio minVideo maxVideo"; sin guardar, al menos 1 minuto.
    durations = { 60.0, 0.0, 60.0, 0.0 };
    Statement s (db, "SELECT value FROM settings WHERE key = 'durations'");
    if (s.next())
    {
        juce::StringArray v;
        v.addTokens (s.text (0), " ", "");
        if (v.size() == 4)
            durations = { v[0].getDoubleValue(), v[1].getDoubleValue(), v[2].getDoubleValue(), v[3].getDoubleValue() };
    }
    return true;
}

bool Library::DurationLimits::accepts (double seconds, bool video) const
{
    const double low = video ? minVideo : minAudio, high = video ? maxVideo : maxAudio;
    return seconds >= low && (high <= 0.0 || seconds <= high);
}

Library::DurationLimits Library::getDurationLimits() const
{
    const juce::ScopedLock sl (lock);
    return durations;
}

void Library::setDurationLimits (const DurationLimits& limits)
{
    {
        const juce::ScopedLock sl (lock);
        if (limits == durations)
            return;
        durations = limits;
        Statement (db, "INSERT OR REPLACE INTO settings (key, value) VALUES ('durations', ?)")
            .bind (1, juce::String (limits.minAudio) + " " + juce::String (limits.maxAudio) + " "
                          + juce::String (limits.minVideo) + " " + juce::String (limits.maxVideo)).run();
    }
    startPendingCount();   // lo que vuelve a verse puede estar sin analizar
    if (worker != nullptr)
        worker->notify();
    contentChanged();
}

bool Library::acceptsFile (const juce::File& file) const
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return false;
    return getDurationLimits().accepts ((double) reader->lengthInSamples / reader->sampleRate, isVideoFile (file));
}

juce::String Library::visibleClause() const
{
    const auto d = getDurationLimits();
    // Los videos son los de isVideoFile (todas sus extensiones tienen 3 letras).
    auto range = [] (double low, double high)
    {
        juce::String r = "duration >= " + juce::String (low);
        if (high > 0.0)
            r << " AND duration <= " << juce::String (high);
        return r;
    };
    return "hidden = 0 AND (CASE WHEN lower (substr (path, -4)) IN ('.mp4', '.mov', '.m4v', '.wmv', '.3gp') THEN "
           + range (d.minVideo, d.maxVideo) + " ELSE " + range (d.minAudio, d.maxAudio) + " END)";
}

void Library::close()
{
    if (worker != nullptr)
    {
        worker->signalThreadShouldExit();
        worker->notify();
        worker->stopThread (20000);
        worker.reset();
    }
    const juce::ScopedLock sl (lock);
    sqlite3_close (db);
    db = nullptr;
}

bool Library::exec (const char* sql) const
{
    const juce::ScopedLock sl (lock);
    char* error = nullptr;
    const bool ok = db != nullptr && sqlite3_exec (db, sql, nullptr, nullptr, &error) == SQLITE_OK;
    if (error != nullptr)
    {
        DBG ("SQLite: " << error);
        sqlite3_free (error);
    }
    return ok;
}

juce::String Library::normalise (const juce::String& text)
{
    juce::String out;
    out.preallocateBytes (text.getNumBytesAsUTF8());
    for (auto p = text.getCharPointer(); ! p.isEmpty();)
    {
        auto c = juce::CharacterFunctions::toLowerCase (p.getAndAdvance());
        if (c >= 0xc0 && c <= 0xde && c != 0xd7)   // mayusculas acentuadas: towlower de Windows no las baja
            c += 0x20;
        if      (c >= 0xe0 && c <= 0xe5) c = 'a';
        else if (c >= 0xe8 && c <= 0xeb) c = 'e';
        else if (c >= 0xec && c <= 0xef) c = 'i';
        else if (c >= 0xf2 && c <= 0xf6) c = 'o';
        else if (c >= 0xf9 && c <= 0xfc) c = 'u';
        else if (c == 0xf1)              c = 'n';
        else if (c == 0xe7)              c = 'c';
        else if (c == 0xfd || c == 0xff) c = 'y';
        out += c;
    }
    return out;
}

bool Library::isAudioFile (const juce::File& file)
{
    // Mas los de Media Foundation: videos (MP4, MOV, WMV) y M4A/AAC/WMA.
    return file.hasFileExtension ("mp3;wav;flac;ogg;aif;aiff;mp4;m4a;mov;m4v;wmv;wma;aac;3gp");
}

void Library::addFolder (const juce::File& folder)
{
    {
        const juce::ScopedLock sl (lock);
        Statement (db, "INSERT OR IGNORE INTO folders (path) VALUES (?)").bind (1, folder.getFullPathName()).run();
    }
    includeFolder (folder);
}

void Library::includeFolder (const juce::File& folder)
{
    {
        const juce::ScopedLock sl (lock);
        // Si estaba quitada (entera, en parte o dentro de una carpeta quitada), vuelve con su analisis.
        const auto prefix = folderPrefix (folder);
        Statement (db, "UPDATE tracks SET hidden = 0 WHERE substr (path, 1, length (?1)) = ?1").bind (1, prefix).run();
        Statement (db, "DELETE FROM excluded WHERE substr (path, 1, length (?1)) = ?1 OR substr (?1, 1, length (path)) = path")
            .bind (1, prefix).run();
    }
    contentChanged();
}

void Library::removeFolder (const juce::File& folder)
{
    {
        const juce::ScopedLock sl (lock);
        Statement (db, "DELETE FROM folders WHERE path = ?").bind (1, folder.getFullPathName()).run();
        // Ocultar sus canciones (conservan el analisis por si vuelve) salvo las que sigan dentro de
        // otra carpeta de la biblioteca.
        const auto prefix = folderPrefix (folder);
        Statement (db, "UPDATE tracks SET hidden = 1 WHERE substr (path, 1, length (?1)) = ?1 AND NOT EXISTS "
                       "(SELECT 1 FROM folders f WHERE substr (tracks.path, 1, length (f.path) + 1) = f.path || ?2)")
            .bind (1, prefix).bind (2, juce::String::charToString (juce::File::getSeparatorChar())).run();
    }
    contentChanged();
}

juce::Array<juce::File> Library::getFolders() const
{
    juce::Array<juce::File> result;
    const juce::ScopedLock sl (lock);
    Statement s (db, "SELECT path FROM folders ORDER BY path COLLATE NOCASE");
    while (s.next())
        result.add (juce::File (s.text (0)));
    return result;
}

void Library::startBackgroundWork()
{
    if (db == nullptr)
        return;
    if (worker == nullptr)
    {
        worker = std::make_unique<Worker> (*this);
        worker->startThread (juce::Thread::Priority::low);   // por debajo del audio, el video y la interfaz; el disco, normal
    }
    else
    {
        worker->requestScan();
    }
}

bool Library::isBusy() const
{
    return scanning.load() || pendingDone.load() < pendingTotal.load();
}

juce::String Library::getStatusText() const
{
    if (scanning.load())
        return "Buscando canciones...";
    if (pendingDone.load() < pendingTotal.load())
        return "Analizando BPM: " + juce::String (pendingDone.load() + 1) + " de " + juce::String (pendingTotal.load());
    return juce::String (getNumTracks()) + " canciones";
}

void Library::startPendingCount()
{
    const juce::ScopedLock sl (lock);
    Statement s (db, ("SELECT COUNT(*) FROM tracks WHERE analysed = 0 AND " + visibleClause()).toRawUTF8());
    pendingTotal = s.next() ? (int) s.integer (0) : 0;
    pendingDone = 0;
}

void Library::fixMpeg2Durations (juce::Thread* thread)
{
    std::vector<std::pair<juce::int64, juce::String>> mp3s;
    {
        const juce::ScopedLock sl (lock);
        if (Statement (db, "SELECT 1 FROM settings WHERE key = 'mpeg2_durations'").next())
            return;   // ya hecho
        Statement s (db, "SELECT id, path FROM tracks WHERE lower (substr (path, -4)) = '.mp3'");
        while (s.next())
            mp3s.emplace_back (s.integer (0), s.text (1));
    }
    // Fuera del cerrojo: solo la cabecera de cada MP3 (64 KB) y, si es MPEG-2, su duracion.
    std::vector<std::pair<juce::int64, double>> fixes;
    for (const auto& [id, path] : mp3s)
    {
        if (thread != nullptr && thread->threadShouldExit())
            return;   // sin marcar: se termina la proxima vez
        juce::FileInputStream in ((juce::File (path)));
        if (! in.openedOk() || Mp3Format::samplesPerFrame (in) != 576)
            continue;
        if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (juce::File (path)) }; reader != nullptr && reader->sampleRate > 0.0)
            fixes.emplace_back (id, (double) reader->lengthInSamples / reader->sampleRate);
    }
    {
        const juce::ScopedLock sl (lock);
        exec ("BEGIN");
        for (const auto& [id, seconds] : fixes)
            Statement (db, "UPDATE tracks SET duration = ? WHERE id = ?").bind (1, seconds).bind (2, id).run();
        Statement (db, "INSERT OR REPLACE INTO settings (key, value) VALUES ('mpeg2_durations', ?)")
            .bind (1, juce::String ((int) fixes.size())).run();
        exec ("COMMIT");
    }
    if (! fixes.empty())
        contentChanged();
}

void Library::fixLegacyTagText (juce::Thread* thread)
{
    // Hasta la 0.5.0 las etiquetas de 8 bits se leian siempre como Latin-1: las de la pagina 850 de
    // DOS salian con simbolos ("Ma¤anita") y las UTF-8 con dos letras raras ("MaÃ±anita"). Una vez:
    // se vuelven a leer las que tienen esos caracteres.
    struct Row { juce::int64 id; juce::String path, title, artist, album, genre; };
    std::vector<Row> rows;
    auto suspicious = [] (const juce::String& s)
    {
        for (auto c : s)
            if (c >= 0x80 && c <= 0xc3)   // controles, simbolos de Latin-1 y la Ã de un UTF-8 mal leido
                return true;
        return false;
    };
    {
        const juce::ScopedLock sl (lock);
        if (Statement (db, "SELECT 1 FROM settings WHERE key = 'tag_text_dos'").next())
            return;   // ya hecho
        Statement s (db, "SELECT id, path, title, artist, album, genre FROM tracks");
        while (s.next())
        {
            Row r { s.integer (0), s.text (1), s.text (2), s.text (3), s.text (4), s.text (5) };
            if (suspicious (r.title) || suspicious (r.artist) || suspicious (r.album) || suspicious (r.genre))
                rows.push_back (std::move (r));
        }
    }
    std::vector<std::pair<juce::int64, TrackTags>> fixes;
    std::vector<juce::String> searches;
    for (const auto& r : rows)
    {
        if (thread != nullptr && thread->threadShouldExit())
            return;   // sin marcar: se termina la proxima vez
        const juce::File file (r.path);
        if (! file.existsAsFile())
            continue;
        const auto tags = TagReader::read (file);
        if (tags.title == r.title && tags.artist == r.artist && tags.album == r.album && tags.genre == r.genre)
            continue;
        searches.push_back (normalise (tags.title + " " + tags.artist + " " + tags.album + " " + tags.genre + " " + file.getFileNameWithoutExtension()));
        fixes.emplace_back (r.id, tags);
    }
    {
        const juce::ScopedLock sl (lock);
        exec ("BEGIN");
        for (size_t i = 0; i < fixes.size(); ++i)
        {
            const auto& [id, tags] = fixes[i];
            Statement (db, "UPDATE tracks SET title = ?, artist = ?, album = ?, genre = ?, search = ? WHERE id = ?")
                .bind (1, tags.title).bind (2, tags.artist).bind (3, tags.album).bind (4, tags.genre).bind (5, searches[i]).bind (6, id).run();
        }
        Statement (db, "INSERT OR REPLACE INTO settings (key, value) VALUES ('tag_text_dos', ?)")
            .bind (1, juce::String ((int) fixes.size())).run();
        exec ("COMMIT");
    }
    if (! fixes.empty())
        contentChanged();
}

void Library::scanNow()
{
    for (const auto& folder : getFolders())
        scanFolder (folder, nullptr);
    startPendingCount();
}

int Library::analysePendingNow (int maxTracks)
{
    int done = 0;
    while (done < maxTracks && analyseOne (nullptr))
        ++done;
    return done;
}

void Library::scanFolder (const juce::File& folder, juce::Thread* thread)
{
    if (! folder.isDirectory())
        return;

    // Lo que ya hay en la base de esta carpeta: ruta -> (tamano, fecha).
    std::map<juce::String, std::pair<juce::int64, juce::int64>> known;
    const auto prefix = folderPrefix (folder);
    {
        const juce::ScopedLock sl (lock);
        Statement s (db, "SELECT path, file_size, file_mtime FROM tracks WHERE substr (path, 1, length (?1)) = ?1");
        s.bind (1, prefix);
        while (s.next())
            known[s.text (0)] = { s.integer (1), s.integer (2) };
    }

    std::set<juce::String> seen;
    const auto exclusions = getExclusions();
    int changes = 0;
    for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*", juce::File::findFiles))
    {
        if (thread != nullptr && thread->threadShouldExit())
            return;
        const auto file = entry.getFile();
        if (! isAudioFile (file))
            continue;
        const auto path = file.getFullPathName();
        const auto size = entry.getFileSize();
        const auto mtime = entry.getModificationTime().toMilliseconds();
        seen.insert (path);
        // Dentro de una carpeta quitada del arbol: no se anade (lo que ya estaba, sigue oculto).
        if (std::any_of (exclusions.begin(), exclusions.end(), [&] (const juce::String& e) { return path.startsWith (e); }))
            continue;

        const auto it = known.find (path);
        if (it != known.end() && it->second.first == size && it->second.second == mtime)
            continue;   // sin cambios desde el ultimo escaneo

        // Lo lento (leer etiquetas y duracion) fuera del cerrojo, para no frenar las busquedas.
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->sampleRate <= 0.0)
            continue;
        const double duration = (double) reader->lengthInSamples / reader->sampleRate;
        reader.reset();
        const auto tags = TagReader::read (file);
        const auto search = normalise (tags.title + " " + tags.artist + " " + tags.album + " " + tags.genre + " "
                                       + file.getFileNameWithoutExtension());
        {
            const juce::ScopedLock sl (lock);
            // Archivo nuevo o cambiado: etiquetas al dia y analisis pendiente otra vez.
            Statement (db, "INSERT INTO tracks (path, title, artist, album, genre, duration, file_size, file_mtime, added, search) "
                           "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10) "
                           "ON CONFLICT (path) DO UPDATE SET title = ?2, artist = ?3, album = ?4, genre = ?5, duration = ?6, "
                           "file_size = ?7, file_mtime = ?8, search = ?10, bpm = 0, first_beat = 0, analysed = 0")
                .bind (1, path).bind (2, tags.title).bind (3, tags.artist).bind (4, tags.album).bind (5, tags.genre)
                .bind (6, duration).bind (7, (juce::int64) size).bind (8, (juce::int64) mtime)
                .bind (9, juce::Time::currentTimeMillis()).bind (10, search).run();
        }
        if (++changes % 20 == 0)
            contentChanged();
    }

    // Archivos que estaban en la base y ya no existen.
    {
        const juce::ScopedLock sl (lock);
        exec ("BEGIN");   // en una sola escritura: borrar una carpeta grande no escribe el disco miles de veces
        for (const auto& [path, info] : known)
            if (seen.count (path) == 0)
                Statement (db, "DELETE FROM tracks WHERE path = ?").bind (1, path).run();
        exec ("COMMIT");
    }
    contentChanged();
}

bool Library::analyseOne (juce::Thread* thread)
{
    juce::int64 id = 0;
    juce::String path;
    {
        const juce::ScopedLock sl (lock);
        // Por el indice (analysed, added): sin ordenar toda la tabla con el cerrojo puesto (con
        // cientos de miles de canciones eso frenaba las busquedas de la interfaz en cada cancion).
        const auto visible = visibleClause();
        for (int attempt = 0; attempt < 2 && id == 0; ++attempt)
        {
            const bool fromCursor = hasCursor && attempt == 0;
            Statement s (db, ("SELECT id, path, added FROM tracks WHERE analysed = 0"
                              + juce::String (fromCursor ? " AND (added, id) > (?1, ?2)" : "")
                              + " AND " + visible + " ORDER BY added, id LIMIT 1").toRawUTF8());
            if (fromCursor)
                s.bind (1, cursorAdded).bind (2, cursorId);
            if (s.next())
            {
                id = s.integer (0);
                path = s.text (1);
                cursorAdded = s.integer (2);
                cursorId = id;
                hasCursor = true;
            }
            else if (! fromCursor)
                break;
        }
        if (id == 0)
        {
            hasCursor = false;
            pendingDone = pendingTotal.load();   // lo que quedaba lo analizo otro (p. ej. un deck)
            return false;
        }
    }

    // Se decodifican los primeros 2 minutos por trozos de 5 s y se mezclan a mono sobre la
    // marcha (como BpmDetector::analyse con un bufer: suma de los dos canales). Asi no se
    // guardan los 2 minutos en estereo (40 MB por cancion, mucho en un equipo con poca memoria)
    // y se puede parar a mitad si se cierra.
    TempoAnalysis analysis;
    bool readable = false;
    if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (juce::File (path)) })
    {
        const double rate = reader->sampleRate;
        const int length = (int) std::min ((double) reader->lengthInSamples, analysisSeconds * rate);
        const int chunk = juce::jmax (1, (int) (5.0 * rate));
        std::vector<float> mono ((size_t) juce::jmax (0, length), 0.0f);
        juce::AudioBuffer<float> part (2, juce::jmin (chunk, juce::jmax (1, length)));
        for (int start = 0; start < length; start += chunk)
        {
            if (thread != nullptr && thread->threadShouldExit())
                return false;
            const int n = juce::jmin (chunk, length - start);
            reader->read (&part, 0, n, start, true, true);
            juce::FloatVectorOperations::add (mono.data() + start, part.getReadPointer (0), part.getReadPointer (1), n);
        }
        analysis = BpmDetector::analyse (mono.data(), length, rate);
        readable = length > 0;
    }
    {
        const juce::ScopedLock sl (lock);
        Statement (db, "UPDATE tracks SET bpm = ?, first_beat = ?, analysed = ? WHERE id = ?")
            .bind (1, analysis.bpm).bind (2, analysis.firstBeat).bind (3, (juce::int64) (readable ? 1 : -1)).bind (4, id).run();
    }
    ++pendingDone;
    sendChangeMessage();   // solo el BPM: la lista no se rehace (ver getContentVersion)
    return true;
}

std::vector<TrackInfo> Library::search (const juce::String& text, const juce::File& folder,
                                        SortColumn sort, bool ascending) const
{
    juce::String sql = juce::String ("SELECT ") + trackColumns + " FROM tracks WHERE " + visibleClause();
    juce::StringArray words;
    words.addTokens (normalise (text), " \t", "\"");
    words.removeEmptyStrings();
    for (int i = 0; i < words.size(); ++i)
        sql << " AND search LIKE ?" << (i + 1) << " ESCAPE '!'";
    const int folderParam = words.size() + 1;
    if (folder != juce::File())
        sql << " AND substr (path, 1, length (?" << folderParam << ")) = ?" << folderParam;

    const char* direction = ascending ? " ASC" : " DESC";
    switch (sort)
    {
        case SortColumn::title:    sql << " ORDER BY title COLLATE NOCASE" << direction; break;
        case SortColumn::artist:   sql << " ORDER BY artist COLLATE NOCASE" << direction << ", title COLLATE NOCASE"; break;
        case SortColumn::bpm:      sql << " ORDER BY bpm" << direction << ", artist COLLATE NOCASE"; break;
        case SortColumn::duration: sql << " ORDER BY duration" << direction; break;
        case SortColumn::album:    sql << " ORDER BY album COLLATE NOCASE" << direction << ", title COLLATE NOCASE"; break;
        case SortColumn::genre:    sql << " ORDER BY genre COLLATE NOCASE" << direction << ", artist COLLATE NOCASE"; break;
        case SortColumn::added:    sql << " ORDER BY added" << direction; break;
    }

    std::vector<TrackInfo> result;
    const juce::ScopedLock sl (lock);
    Statement s (db, sql.toRawUTF8());
    for (int i = 0; i < words.size(); ++i)
        s.bind (i + 1, likePattern (words[i]));
    if (folder != juce::File())
        s.bind (folderParam, folderPrefix (folder));
    while (s.next())
        result.push_back (readTrack (s));
    return result;
}

std::optional<TrackInfo> Library::findByFile (const juce::File& file) const
{
    const juce::ScopedLock sl (lock);
    Statement s (db, (juce::String ("SELECT ") + trackColumns + " FROM tracks WHERE path = ?").toRawUTF8());
    s.bind (1, file.getFullPathName());
    if (s.next())
        return readTrack (s);
    return std::nullopt;
}

int Library::getNumTracks() const
{
    const juce::ScopedLock sl (lock);
    Statement s (db, ("SELECT COUNT(*) FROM tracks WHERE " + visibleClause()).toRawUTF8());
    return s.next() ? (int) s.integer (0) : 0;
}

juce::StringArray Library::getExclusions() const
{
    juce::StringArray result;
    const juce::ScopedLock sl (lock);
    Statement s (db, "SELECT path FROM excluded");
    while (s.next())
        result.add (s.text (0));
    return result;
}

Library::Removal Library::removeFromLibrary (const juce::File& folder)
{
    Removal removal { folder, {} };
    const auto prefix = folderPrefix (folder);
    {
        const juce::ScopedLock sl (lock);
        // Las carpetas de la biblioteca que estan dentro (o que son esta) dejan de serlo.
        {
            Statement s (db, "SELECT path FROM folders WHERE path = ?1 OR substr (path, 1, length (?2)) = ?2");
            s.bind (1, folder.getFullPathName()).bind (2, prefix);
            while (s.next())
                removal.roots.add (juce::File (s.text (0)));
        }
        for (const auto& root : removal.roots)
            Statement (db, "DELETE FROM folders WHERE path = ?").bind (1, root.getFullPathName()).run();
        // Si queda dentro de otra carpeta de la biblioteca, se apunta para que no vuelva al re-escanear.
        Statement (db, "INSERT OR IGNORE INTO excluded (path) VALUES (?)").bind (1, prefix).run();
        Statement (db, "UPDATE tracks SET hidden = 1 WHERE substr (path, 1, length (?1)) = ?1").bind (1, prefix).run();
    }
    contentChanged();
    return removal;
}

void Library::undoRemoval (const Removal& removal)
{
    const auto prefix = folderPrefix (removal.folder);
    {
        const juce::ScopedLock sl (lock);
        Statement (db, "DELETE FROM excluded WHERE path = ?").bind (1, prefix).run();
        Statement (db, "UPDATE tracks SET hidden = 0 WHERE substr (path, 1, length (?1)) = ?1").bind (1, prefix).run();
        for (const auto& root : removal.roots)
            Statement (db, "INSERT OR IGNORE INTO folders (path) VALUES (?)").bind (1, root.getFullPathName()).run();
    }
    contentChanged();
}

void Library::setHidden (const juce::StringArray& paths, bool hidden)
{
    {
        const juce::ScopedLock sl (lock);
        for (const auto& path : paths)
            Statement (db, "UPDATE tracks SET hidden = ? WHERE path = ?").bind (1, (juce::int64) (hidden ? 1 : 0)).bind (2, path).run();
    }
    contentChanged();
}

juce::StringArray Library::getTrackPaths() const
{
    juce::StringArray result;
    const juce::ScopedLock sl (lock);
    Statement s (db, ("SELECT path FROM tracks WHERE " + visibleClause()).toRawUTF8());
    while (s.next())
        result.add (s.text (0));
    return result;
}

bool Library::isVideoFile (const juce::File& file)
{
    return file.hasFileExtension ("mp4;mov;m4v;wmv;3gp");
}

juce::Array<juce::File> Library::localDrives()
{
    juce::Array<juce::File> roots, result;
    juce::File::findFileSystemRoots (roots);
    for (const auto& r : roots)
        if (r.isDirectory() && r.isOnHardDisk())
            result.add (r);
    return result;
}

juce::String Library::driveName (const juce::File& drive)
{
    const auto letter = drive.getFullPathName().upToFirstOccurrenceOf (":", true, false);
    const auto label = drive.getVolumeLabel().trim();
    return (label.isNotEmpty() ? label : juce::String ("Disco local")) + " (" + letter + ")";
}

juce::Array<juce::File> Library::findMediaFolders (const SyncOptions& options,
                                                   const std::function<void (const juce::File&, int)>& progress,
                                                   const std::function<bool()>& shouldStop, int& filesFound)
{
    // Carpetas del sistema o de programas: nunca tienen la musica de nadie y son enormes.
    static const juce::StringArray skipNames { "windows", "program files", "program files (x86)", "programdata", "appdata",
                                               "system volume information", "recovery", "perflogs", "msocache", "config.msi",
                                               "node_modules", "$recycle.bin", "$windows.~bt", "$windows.~ws", "windowsapps",
                                               "intel", "amd", "nvidia", "drivers", "boot", "efi",
                                               // juegos y sus sonidos
                                               "steamapps", "steamlibrary", "epic games", "riot games", "xboxgames",
                                               "origin games", "ea games", "gog games", "ubisoft game launcher" };
    juce::Array<juce::File> found;
    filesFound = 0;
    std::function<void (const juce::File&, bool)> visit = [&] (const juce::File& dir, bool isRoot)
    {
        if (shouldStop())
            return;
        if (progress)
            progress (dir, filesFound);
        int here = 0;
        bool accepted = ! options.accept;   // sin filtro, cualquier archivo vale
        juce::Array<juce::File> subdirs;
        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
        {
            const auto f = entry.getFile();
            if (entry.isDirectory())
            {
                const auto name = f.getFileName().toLowerCase();
                if (! name.startsWithChar ('.') && ! name.startsWithChar ('$') && ! skipNames.contains (name))
                    subdirs.add (f);
            }
            else if (isAudioFile (f) && (isVideoFile (f) ? options.video : options.audio))
            {
                ++here;
                // Con filtro (duracion): se abren archivos hasta que uno valga.
                if (! accepted && ! isRoot && ! shouldStop())
                    accepted = options.accept (f);
            }
        }
        // Los archivos sueltos en la raiz de un disco no cuentan (seria escanear el disco entero).
        if (here > 0 && accepted && ! isRoot)
        {
            found.add (dir);
            filesFound += here;
        }
        for (const auto& sub : subdirs)
            visit (sub, false);
    };
    for (const auto& root : options.roots)
        visit (root, root.getParentDirectory() == root);

    // Sin repetir: una carpeta dentro de otra encontrada ya entra con ella (se escanea con subcarpetas).
    juce::Array<juce::File> result;
    for (const auto& f : found)
        if (! std::any_of (found.begin(), found.end(), [&] (const juce::File& other) { return other != f && f.isAChildOf (other); }))
            result.add (f);
    return result;
}

void Library::setAnalysis (const juce::File& file, const TempoAnalysis& analysis)
{
    {
        const juce::ScopedLock sl (lock);
        Statement (db, "UPDATE tracks SET bpm = ?, first_beat = ?, analysed = 1 WHERE path = ?")
            .bind (1, analysis.bpm).bind (2, analysis.firstBeat).bind (3, file.getFullPathName()).run();
    }
    sendChangeMessage();   // solo el BPM
}

bool Library::updateAnalysis (TrackInfo& track) const
{
    const juce::ScopedLock sl (lock);
    Statement s (db, "SELECT bpm, first_beat, analysed FROM tracks WHERE id = ?");
    s.bind (1, track.id);
    if (! s.next())
        return false;
    const double bpm = s.real (0), firstBeat = s.real (1);
    const bool analysed = s.integer (2) == 1;
    if (bpm == track.bpm && firstBeat == track.firstBeat && analysed == track.analysed)
        return false;
    track.bpm = bpm;
    track.firstBeat = firstBeat;
    track.analysed = analysed;
    return true;
}

void Library::setFavorite (const juce::File& file, bool favorite)
{
    {
        const juce::ScopedLock sl (lock);
        Statement (db, "UPDATE tracks SET favorite = ? WHERE path = ?")
            .bind (1, (juce::int64) (favorite ? 1 : 0)).bind (2, file.getFullPathName()).run();
    }
    contentChanged();
}

void Library::addPlay (const juce::File& file)
{
    {
        const juce::ScopedLock sl (lock);
        Statement (db, "UPDATE tracks SET plays = plays + 1, last_played = ? WHERE path = ?")
            .bind (1, (juce::int64) juce::Time::currentTimeMillis()).bind (2, file.getFullPathName()).run();
    }
    ++playsVersion;        // solo cambia "Mas reproducidos": el resto de la lista no se rehace
    sendChangeMessage();
}

std::vector<TrackInfo> Library::getFavorites() const
{
    std::vector<TrackInfo> result;
    const juce::ScopedLock sl (lock);
    Statement s (db, (juce::String ("SELECT ") + trackColumns
                      + " FROM tracks WHERE " + visibleClause() + " AND favorite = 1 ORDER BY artist COLLATE NOCASE, title COLLATE NOCASE").toRawUTF8());
    while (s.next())
        result.push_back (readTrack (s));
    return result;
}

std::vector<TrackInfo> Library::getMostPlayed (int limit) const
{
    std::vector<TrackInfo> result;
    const juce::ScopedLock sl (lock);
    Statement s (db, (juce::String ("SELECT ") + trackColumns
                      + " FROM tracks WHERE " + visibleClause() + " AND plays > 0 ORDER BY plays DESC, last_played DESC LIMIT ?").toRawUTF8());
    s.bind (1, (juce::int64) limit);
    while (s.next())
        result.push_back (readTrack (s));
    return result;
}

int Library::countFavorites() const
{
    const juce::ScopedLock sl (lock);
    Statement s (db, ("SELECT COUNT(*) FROM tracks WHERE " + visibleClause() + " AND favorite = 1").toRawUTF8());
    return s.next() ? (int) s.integer (0) : 0;
}

int Library::countPlayed() const
{
    const juce::ScopedLock sl (lock);
    Statement s (db, ("SELECT COUNT(*) FROM tracks WHERE " + visibleClause() + " AND plays > 0").toRawUTF8());
    return s.next() ? juce::jmin (50, (int) s.integer (0)) : 0;
}

std::vector<Library::Playlist> Library::getPlaylists() const
{
    std::vector<Playlist> result;
    const juce::ScopedLock sl (lock);
    Statement s (db, "SELECT p.id, p.name, (SELECT COUNT(*) FROM playlist_items i JOIN tracks t ON t.id = i.track_id"
                     " WHERE i.playlist_id = p.id AND t.hidden = 0) FROM playlists p ORDER BY p.name COLLATE NOCASE");
    while (s.next())
        result.push_back ({ s.integer (0), s.text (1), (int) s.integer (2) });
    return result;
}

juce::int64 Library::createPlaylist (const juce::String& name)
{
    juce::int64 id = 0;
    {
        const juce::ScopedLock sl (lock);
        if (Statement (db, "INSERT INTO playlists (name) VALUES (?)").bind (1, name).run())
            id = sqlite3_last_insert_rowid (db);
    }
    contentChanged();
    return id;
}

void Library::renamePlaylist (juce::int64 id, const juce::String& name)
{
    {
        const juce::ScopedLock sl (lock);
        Statement (db, "UPDATE playlists SET name = ? WHERE id = ?").bind (1, name).bind (2, id).run();
    }
    contentChanged();
}

void Library::deletePlaylist (juce::int64 id)
{
    {
        const juce::ScopedLock sl (lock);
        Statement (db, "DELETE FROM playlist_items WHERE playlist_id = ?").bind (1, id).run();
        Statement (db, "DELETE FROM playlists WHERE id = ?").bind (1, id).run();
    }
    contentChanged();
}

bool Library::addToPlaylist (juce::int64 id, const juce::File& file)
{
    bool added = false;
    {
        const juce::ScopedLock sl (lock);
        Statement find (db, "SELECT id FROM tracks WHERE path = ?");
        find.bind (1, file.getFullPathName());
        if (! find.next())
            return false;
        const auto trackId = find.integer (0);
        Statement last (db, "SELECT COALESCE (MAX (position), -1) FROM playlist_items WHERE playlist_id = ?");
        last.bind (1, id);
        const auto position = last.next() ? last.integer (0) + 1 : 0;
        Statement insert (db, "INSERT OR IGNORE INTO playlist_items (playlist_id, track_id, position) VALUES (?, ?, ?)");
        insert.bind (1, id).bind (2, trackId).bind (3, position);
        added = insert.run() && sqlite3_changes (db) > 0;
    }
    if (added)
        contentChanged();
    return added;
}

void Library::removeFromPlaylist (juce::int64 id, juce::int64 trackId)
{
    {
        const juce::ScopedLock sl (lock);
        Statement (db, "DELETE FROM playlist_items WHERE playlist_id = ? AND track_id = ?").bind (1, id).bind (2, trackId).run();
    }
    contentChanged();
}

void Library::moveInPlaylist (juce::int64 id, juce::int64 trackId, int newIndex)
{
    // Se rehace el orden entero (las listas son cortas).
    auto tracks = getPlaylistTracks (id);
    const auto it = std::find_if (tracks.begin(), tracks.end(), [trackId] (const TrackInfo& t) { return t.id == trackId; });
    if (it == tracks.end())
        return;
    const auto moved = *it;
    tracks.erase (it);
    tracks.insert (tracks.begin() + juce::jlimit (0, (int) tracks.size(), newIndex), moved);
    {
        const juce::ScopedLock sl (lock);
        exec ("BEGIN");
        for (size_t i = 0; i < tracks.size(); ++i)
            Statement (db, "UPDATE playlist_items SET position = ? WHERE playlist_id = ? AND track_id = ?")
                .bind (1, (juce::int64) i).bind (2, id).bind (3, tracks[i].id).run();
        exec ("COMMIT");
    }
    contentChanged();
}

std::vector<TrackInfo> Library::getPlaylistTracks (juce::int64 id) const
{
    std::vector<TrackInfo> result;
    const juce::ScopedLock sl (lock);
    Statement s (db, (juce::String ("SELECT ") + trackColumns + " FROM playlist_items i JOIN tracks ON tracks.id = i.track_id"
                      " WHERE i.playlist_id = ? AND hidden = 0 ORDER BY i.position").toRawUTF8());
    s.bind (1, id);
    while (s.next())
        result.push_back (readTrack (s));
    return result;
}
}
