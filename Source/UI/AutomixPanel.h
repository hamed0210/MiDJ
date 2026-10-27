#pragma once

#include "Engine/Automix.h"
#include "Library/Library.h"
#include "UI/Icons.h"
#include "UI/MidiLearnOverlay.h"
#include "UI/YouTubePanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// Lista de reproduccion del automix en su propio panel: las canciones en orden, encender /
// apagar, mezclar ya, saltar, quitar, vaciar y la duracion del fundido. Se anaden arrastrando
// canciones desde la lista (o con el menu de una cancion > Agregar a automix), o con "Listas":
// reproducir o anadir los Favoritos, los Mas reproducidos o una lista propia.
// Dos pestanas: AUTOMIX (los decks) y YOUTUBE (el mismo automix con su lista aparte, que suena
// en el reproductor de YouTube); Encendido / Apagado es el de la pestana que se ve.
class AutomixPanel : public juce::Component,
                     public juce::DragAndDropTarget,
                     private juce::ChangeListener,
                     private juce::ListBoxModel
{
public:
    AutomixPanel (Automix& automix, Library& library);
    ~AutomixPanel() override;

    void updateStatus();   // la ventana lo llama a menudo (cuenta atras, avance)
    void addLearnTargets (std::vector<LearnTarget>& out);
    // La pestana YouTube (el panel y su lista; los crea la ventana).
    void setYouTube (YouTubePanel* panel, YouTubeQueue* queue);
    void showYouTubeTab (bool show);

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;

    // Canciones de la biblioteca (al final) o filas de la propia lista (a otro sitio).
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails& details) override;
    void itemDragMove (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails& details) override;
    static constexpr const char* rowDragPrefix = "automix-row:";

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override;
    void deleteKeyPressed (int row) override;
    juce::String getTooltipForRow (int row) override;   // el titulo entero
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    void moveRow (int from, int to);   // y la seleccion la sigue
    void showListsMenu();

    Automix& automix;
    Library& library;
    bool dragOver = false;
    int dropGap = -1;   // hueco donde caeria una fila arrastrada (-1 = ninguno)
    juce::Label title { {}, "AUTOMIX" }, status;
    juce::ListBox list;
    juce::TextButton onButton { "Apagado" }, listsButton { "Listas" };
    IconButton mixNowButton { Icons::Glyph::mix, "Mezclar ya con la siguiente" }, skipButton { Icons::Glyph::skip, "Saltar la siguiente cancion" },
               clearButton { Icons::Glyph::trash, "Vaciar la lista" };
    juce::ComboBox fadeBox;
    juce::TextButton localTab { "AUTOMIX" }, youtubeTab { "YOUTUBE" };
    juce::Rectangle<int> tabsArea;
    YouTubePanel* youtube = nullptr;
    YouTubeQueue* youtubeQueue = nullptr;
    bool youtubeShown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomixPanel)
};
}
