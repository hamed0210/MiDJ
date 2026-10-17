#pragma once

#include "Engine/Automix.h"
#include "Library/Library.h"
#include "UI/MidiLearnOverlay.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// Navegador de la biblioteca: carpetas a la izquierda, buscador y lista de canciones.
// Una cancion se carga arrastrandola a un deck o con doble clic.
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
    // Menu de una cancion: cargar en un deck concreto (0 = A, 1 = B).
    std::function<void (const TrackInfo&, int deckIndex)> onLoadToDeck;
    // Menu de una cancion: preparar sus stems en segundo plano.
    std::function<void (const TrackInfo&)> onPrepareStems;

    // La lista de automix aparece como un origen mas, con su barra de control.
    void setAutomix (Automix* automix);
    void updateAutomixStatus();   // la ventana lo llama a menudo (cuenta atras, avance)
    // Controles asignables por MIDI: la lista (mover) y los botones del automix.
    void addLearnTargets (std::vector<LearnTarget>& out);

    // Descripcion de arrastre: prefijo + ruta del archivo. Los decks la reconocen.
    static constexpr const char* dragPrefix = "midj-track:";

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Controladora: mover la seleccion y saber cual es (para cargarla en un deck).
    void moveSelection (int steps);
    std::optional<TrackInfo> getSelectedTrack() const;
    juce::Component& getTrackList() { return table; }

    // Para pruebas y para la interfaz: las canciones que se ven ahora.
    const std::vector<TrackInfo>& getVisibleTracks() const { return tracks; }

private:
    enum Columns { titleCol = 1, artistCol, bpmCol, durationCol, albumCol, genreCol };

    // Lista de origenes: "Toda la musica" y cada carpeta.
    struct SourceList : juce::ListBoxModel
    {
        explicit SourceList (LibraryBrowser& b) : owner (b) {}
        int getNumRows() override { return 1 + owner.folders.size() + (owner.automix != nullptr ? 1 : 0); }
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
    bool isAutomixView() const { return automix != nullptr && sourceList.getSelectedRow() == folders.size() + 1; }
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

    Automix* automix = nullptr;
    juce::TextButton automixButton { "AUTOMIX" }, mixNowButton { "Mezclar ya" }, skipButton { "Saltar" },
                     removeButton { "Quitar" }, clearButton { "Vaciar" };
    juce::ComboBox fadeBox;
    juce::Label automixStatus;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryBrowser)
};
}
