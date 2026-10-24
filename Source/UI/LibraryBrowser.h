#pragma once

#include "Engine/Automix.h"
#include "Library/Library.h"
#include "UI/Icons.h"
#include "UI/MidiLearnOverlay.h"
#include "UI/Widgets.h"
#include "UI/YouTubeThumbnails.h"
#include "Video/YouTubeSearch.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// Biblioteca (diseno "Escenario"): a la izquierda el arbol de carpetas, como el de VirtualDJ:
//  - Musica local, con Musica (solo audio) y Videos (solo video);
//  - Mis carpetas: las que se anaden a mano (con su x para quitarlas);
//  - Discos: cada disco y las carpetas donde hay canciones de la biblioteca (tambien con x).
// Arriba del arbol, sincronizar (buscar musica y videos en todo el PC) y anadir carpeta; al
// quitar algo sale un aviso con Deshacer. Una barrita cambia el ancho del arbol. A la derecha,
// el buscador y la lista; cada cancion con A / B (cargar en ese deck) y + (al automix). Doble
// clic: cargar en el deck libre. Tambien se arrastran a un deck, al automix o al sampler.
class LibraryBrowser : public juce::Component,
                       private juce::ChangeListener,
                       private juce::Timer,
                       private juce::TableListBoxModel
{
public:
    explicit LibraryBrowser (Library& library);
    ~LibraryBrowser() override;

    // Doble clic en una cancion.
    std::function<void (const TrackInfo&)> onLoadRequest;
    // Intro en la lista: cargar la cancion elegida (en el deck activo).
    std::function<void (const TrackInfo&)> onReturnKey;
    // Botones A / B y menu de una cancion: cargar en un deck concreto (0 = A, 1 = B).
    std::function<void (const TrackInfo&, int deckIndex)> onLoadToDeck;
    // Menu de una cancion: preparar sus stems en segundo plano.
    std::function<void (const TrackInfo&)> onPrepareStems;
    // YouTube (el arbol > YouTube): ver ya un video, o anadirlo a la lista del automix de YouTube
    // (al final o como el siguiente).
    std::function<void (const YouTubeVideo&)> onYouTubePlay;
    std::function<void (const YouTubeVideo&, bool next)> onYouTubeQueue;
    // El video de YouTube elegido en la lista (si se esta viendo YouTube).
    std::optional<YouTubeVideo> getSelectedYouTube() const;
    static bool isYouTube (const TrackInfo& t) { return t.path.startsWith ("youtube:"); }

    // Para el boton + y el menu de una cancion (Agregar a automix); la lista esta en su propio panel.
    void setAutomix (Automix* a) { automix = a; }
    // Controles asignables por MIDI: la lista (mover).
    void addLearnTargets (std::vector<LearnTarget>& out);

    // Descripcion de arrastre: prefijo + ruta del archivo. Los decks la reconocen.
    static constexpr const char* dragPrefix = "midj-track:";

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Controladora y teclado: mover la seleccion y saber cual es (para cargarla en un deck).
    void moveSelection (int steps);
    std::optional<TrackInfo> getSelectedTrack() const;
    juce::Component& getTrackList() { return table; }
    void focusSearch() { searchBox.grabKeyboardFocus(); }

    // Ancho del arbol de carpetas (para guardarlo).
    int getFoldersWidth() const { return treeWidth; }
    void setFoldersWidth (int w) { treeWidth = juce::jlimit (minTreeWidth, maxTreeWidth, w); resized(); }
    static constexpr int defaultTreeWidth = 240, minTreeWidth = 180, maxTreeWidth = 440;

    // Para pruebas y para la interfaz: las canciones que se ven ahora.
    const std::vector<TrackInfo>& getVisibleTracks() const { return tracks; }

    // Sincronizar con el PC (lo usa la ventanita de opciones).
    void startSync (const Library::SyncOptions& options);

private:
    enum Columns { typeCol = 1, titleCol, artistCol, bpmCol, durationCol, loadCol, favCol };
    class Node;
    class RowButtons;
    class SyncJob;

    void refresh();
    void rebuildTree (bool force);
    void selectNode (Node* node);
    void removeNode (Node& node);
    void showSyncOptions();
    void finishSync (juce::Array<juce::File> folders, int files, bool stopped);
    void showToast (const juce::String& text, bool canUndo);
    void chooseFolder();
    static Automix::Item itemFor (const TrackInfo& t) { return { t.getFile(), t.title, t.artist, t.bpm }; }
    void showTrackMenu (int row);
    void toggleFavorite (const TrackInfo& track);
    // Quitar de la biblioteca las filas elegidas (o 'row' si no esta entre ellas), con Deshacer.
    void hideTracks (int row);
    // Listas de reproduccion: menu de la carpeta "Listas" (id 0) o de una lista; anadir canciones.
    void showPlaylistMenu (juce::int64 id, const juce::String& name);
    void addToPlaylist (juce::int64 id, const juce::File& file);
    void createPlaylistThen (std::function<void (juce::int64)> then);
    void addPlaylistToAutomix (juce::int64 id, bool replace);
    juce::String selectionName() const;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    // juce::TableListBoxModel
    int getNumRows() override { return (int) tracks.size(); }
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    juce::Component* refreshComponentForCell (int row, int column, bool selected, juce::Component* existing) override;
    void sortOrderChanged (int columnId, bool forwards) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void cellClicked (int row, int column, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    void deleteKeyPressed (int row) override { hideTracks (row); }
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    juce::String getCellTooltip (int row, int column) override;   // el titulo y el artista enteros

    Library& library;
    std::vector<TrackInfo> tracks;
    Library::SortColumn sort = Library::SortColumn::artist;
    bool ascending = true;

    // Arbol.
    juce::TreeView tree;
    std::unique_ptr<Node> rootNode;
    juce::String treeSignature;           // para no rehacer el arbol si no cambio nada
    juce::File filterFolder;              // lo elegido en el arbol
    int filterKind = 0;                   // 0 todo, 1 solo audio, 2 solo video, 3 favoritos, 4 mas reproducidos, 5 una lista
    juce::int64 filterPlaylist = 0;       // la lista elegida (filterKind 5)
    bool openPlaylists = false;           // desplegar "Listas" al rehacer el arbol (lista nueva)
    // YouTube (filterKind 6): los resultados de la ultima busqueda y su estado.
    std::vector<YouTubeVideo> youtubeResults;
    juce::String youtubeStatus { "Escribe y pulsa Intro para buscar en YouTube (o pega un enlace)" };
    int youtubeSearchId = 0;
    // Los siguientes resultados (al bajar hasta el final): el codigo de YouTube ("" si no hay mas).
    juce::String youtubeQuery, youtubeContinuation, youtubeClientVersion;
    bool youtubeLoadingMore = false;
    void startYouTubeSearch (const juce::String& text);
    void loadMoreYouTube();
    juce::String youtubeCountText() const;   // "40 videos para "..." (baja para ver mas)"
    std::optional<YouTubeVideo> youTubeAt (int row) const;
    juce::String filterLabel { "Musica local" };
    IconButton syncButton { Icons::Glyph::sync, "Sincronizar: buscar musica y videos en todo el PC y anadirlos" },
               addFolderButton { Icons::Glyph::folderPlus, "Anadir una carpeta" };

    // Aviso de abajo (quitar con Deshacer, resultado de sincronizar).
    juce::Label toastLabel;
    juce::TextButton undoButton { "Deshacer" };
    IconButton closeToastButton { Icons::Glyph::close, "Cerrar el aviso" };
    std::optional<Library::Removal> lastRemoval;
    juce::StringArray lastHidden;         // canciones quitadas sueltas (para Deshacer)
    juce::uint32 toastShownAt = 0;

    // Sincronizacion en marcha.
    std::unique_ptr<SyncJob> syncJob;
    juce::TextButton stopSyncButton { "Detener" };

    juce::TextEditor searchBox;
    IconButton clearSearchButton { Icons::Glyph::close, "Limpiar el buscador (Esc)" };
    void clearSearch();
    juce::TableListBox table { "canciones", this };
    juce::SharedResourcePointer<YouTubeThumbnails> thumbnails;
    bool youtubeLayout = false;   // la lista con miniaturas (filas mas altas)
    ResizeHandle treeHandle;
    int treeWidth = defaultTreeWidth, dragStartWidth = 0;
    juce::Rectangle<int> treeHeaderArea, syncArea, toastArea, footerArea, listArea;

    Automix* automix = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryBrowser)
};
}
