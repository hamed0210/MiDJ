#include "Library/Library.h"
#include "Library/TagReader.h"

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
        CREATE INDEX IF NOT EXISTS tracks_pending ON tracks (analysed);
        CREATE TABLE IF NOT EXISTS excluded (       -- carpetas quitadas del arbol (no se re-escanean)
            path  TEXT PRIMARY KEY);
    )sql";

    constexpr const char* trackColumns = "id, path, title, artist, album, genre, duration, bpm, first_beat, analysed";

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
                library.sendChangeMessage();
                for (const auto& folder : library.getFolders())
                    if (! threadShouldExit())
                        library.scanFolder (folder, this);
                library.scanning = false;
                library.startPendingCount();
                library.sendChangeMessage();
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
    // Bases de versiones anteriores: columna para las canciones ocultas (carpetas quitadas).
    {
        const bool hasHidden = Statement (db, "SELECT 1 FROM pragma_table_info ('tracks') WHERE name = 'hidden'").next();
        if (! hasHidden)
            exec ("ALTER TABLE tracks ADD COLUMN hidden INTEGER DEFAULT 0");
    }
    return true;
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
        // Si estaba quitada (entera o en parte), vuelve con su analisis.
        const auto prefix = folderPrefix (folder);
        Statement (db, "UPDATE tracks SET hidden = 0 WHERE substr (path, 1, length (?1)) = ?1").bind (1, prefix).run();
        Statement (db, "DELETE FROM excluded WHERE substr (path, 1, length (?1)) = ?1").bind (1, prefix).run();
    }
    sendChangeMessage();
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
    sendChangeMessage();
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
        worker->startThread (juce::Thread::Priority::low);
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
    Statement s (db, "SELECT COUNT(*) FROM tracks WHERE analysed = 0 AND hidden = 0");
    pendingTotal = s.next() ? (int) s.integer (0) : 0;
    pendingDone = 0;
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
            sendChangeMessage();
    }

    // Archivos que estaban en la base y ya no existen.
    {
        const juce::ScopedLock sl (lock);
        for (const auto& [path, info] : known)
            if (seen.count (path) == 0)
                Statement (db, "DELETE FROM tracks WHERE path = ?").bind (1, path).run();
    }
    sendChangeMessage();
}

bool Library::analyseOne (juce::Thread* thread)
{
    juce::int64 id = 0;
    juce::String path;
    {
        const juce::ScopedLock sl (lock);
        Statement s (db, "SELECT id, path FROM tracks WHERE analysed = 0 AND hidden = 0 ORDER BY added LIMIT 1");
        if (! s.next())
        {
            pendingDone = pendingTotal.load();   // lo que quedaba lo analizo otro (p. ej. un deck)
            return false;
        }
        id = s.integer (0);
        path = s.text (1);
    }

    // Se decodifican los primeros 2 minutos por trozos, para poder parar a mitad si se cierra.
    TempoAnalysis analysis;
    bool readable = false;
    if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (juce::File (path)) })
    {
        const double rate = reader->sampleRate;
        const int length = (int) std::min ((double) reader->lengthInSamples, analysisSeconds * rate);
        juce::AudioBuffer<float> audio (2, juce::jmax (1, length));
        const int chunk = (int) (5.0 * rate);
        for (int start = 0; start < length; start += chunk)
        {
            if (thread != nullptr && thread->threadShouldExit())
                return false;
            reader->read (&audio, start, juce::jmin (chunk, length - start), start, true, true);
        }
        analysis = BpmDetector::analyse (audio, rate, analysisSeconds);
        readable = length > 0;
    }
    {
        const juce::ScopedLock sl (lock);
        Statement (db, "UPDATE tracks SET bpm = ?, first_beat = ?, analysed = ? WHERE id = ?")
            .bind (1, analysis.bpm).bind (2, analysis.firstBeat).bind (3, (juce::int64) (readable ? 1 : -1)).bind (4, id).run();
    }
    ++pendingDone;
    sendChangeMessage();
    return true;
}

std::vector<TrackInfo> Library::search (const juce::String& text, const juce::File& folder,
                                        SortColumn sort, bool ascending) const
{
    juce::String sql = juce::String ("SELECT ") + trackColumns + " FROM tracks WHERE hidden = 0";
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
    Statement s (db, "SELECT COUNT(*) FROM tracks WHERE hidden = 0");
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
    sendChangeMessage();
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
    sendChangeMessage();
}

juce::StringArray Library::getTrackPaths() const
{
    juce::StringArray result;
    const juce::ScopedLock sl (lock);
    Statement s (db, "SELECT path FROM tracks WHERE hidden = 0");
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
                                               "intel", "amd", "nvidia", "drivers", "boot", "efi" };
    juce::Array<juce::File> found;
    filesFound = 0;
    std::function<void (const juce::File&, bool)> visit = [&] (const juce::File& dir, bool isRoot)
    {
        if (shouldStop())
            return;
        if (progress)
            progress (dir, filesFound);
        int here = 0;
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
                ++here;
        }
        // Los archivos sueltos en la raiz de un disco no cuentan (seria escanear el disco entero).
        if (here > 0 && ! isRoot)
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
    sendChangeMessage();
}
}
