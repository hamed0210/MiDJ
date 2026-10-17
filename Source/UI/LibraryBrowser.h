#pragma once

#include "Engine/Automix.h"
#include "Library/Library.h"
#include "UI/MidiLearnOverlay.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// Navegador de la biblioteca: carpetas a la izquierda (con un divisor para ensancharlas),
// buscador y lista de canciones (con un icono de audio o video). Una cancion se carga
// arrastrandola a un deck o con doble clic.
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
    // Menu de una cancion: cargar en un deck concreto (0 = A, 1 = B).
    std::function<void (const TrackInfo&, int deckIndex)> onLoadToDeck;
    // Menu de una cancion: preparar sus stems en segundo plano.
    std::function<void (const TrackInfo&)> onPrepareStems;

    // Para el menu de una cancion (Agregar a automix); la lista esta en su propio panel.
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

    // Ancho de la columna de carpetas (para guardarlo).
    int getFoldersWidth() const { return sourceList.getWidth(); }
    void setFoldersWidth (int w);

    // Para pruebas y para la interfaz: las canciones que se ven ahora.
    const std::vector<TrackInfo>& getVisibleTracks() const { return tracks; }

private:
    enum Columns { typeCol = 1, titleCol, artistCol, bpmCol, durationCol, albumCol, genreCol };

    // Lista de origenes: "Toda la musica" y cada carpeta.
    struct SourceList : juce::ListBoxModel
    {
        explicit SourceList (LibraryBrowser& b) : owner (b) {}
        int getNumRows() override { return 1 + owner.folders.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void selectedRowsChanged (int) override { owner.refresh(); }
        juce::String getTooltipForRow (int row) override;
        LibraryBrowser& owner;
    };

    void refresh();
    void refreshFolders();
    void chooseFolder();
    void removeSelectedFolder();
    juce::File selectedFolder() const;
    static Automix::Item itemFor (const TrackInfo& t) { return { t.getFile(), t.title, t.artist, t.bpm }; }
    void showTrackMenu (int row);

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    // juce::TableListBoxModel
    int getNumRows() override { return (int) tracks.size(); }
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    void sortOrderChanged (int columnId, bool forwards) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void cellClicked (int row, int column, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;

    Library& library;
    juce::Array<juce::File> folders;
    std::vector<TrackInfo> tracks;
    Library::SortColumn sort = Library::SortColumn::artist;
    bool ascending = true;

    SourceList sourceModel { *this };
    juce::ListBox sourceList { "origenes", &sourceModel };
    juce::TextButton addFolderButton { "+ Carpeta" }, removeFolderButton { "Quitar" };
    juce::Label statusLabel;
    juce::TextEditor searchBox;
    juce::TableListBox table { "canciones", this };

    // Divisor entre las carpetas y la lista (se arrastra a lo ancho).
    juce::StretchableLayoutManager layout;
    juce::StretchableLayoutResizerBar divider { &layout, 1, true };

    Automix* automix = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryBrowser)
};
}
