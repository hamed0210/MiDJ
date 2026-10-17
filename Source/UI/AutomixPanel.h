#pragma once

#include "Engine/Automix.h"
#include "Library/Library.h"
#include "UI/MidiLearnOverlay.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// Lista de reproduccion del automix en su propio panel: las canciones en orden, encender /
// apagar, mezclar ya, saltar, quitar, vaciar y la duracion del fundido. Se anaden arrastrando
// canciones desde la lista (o con el menu de una cancion > Agregar a automix).
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

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;

    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails&) override { dragOver = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dragOver = false; repaint(); }
    void itemDropped (const SourceDetails& details) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override;
    void deleteKeyPressed (int row) override;

    Automix& automix;
    Library& library;
    bool dragOver = false;
    juce::Label title { {}, "AUTOMIX" }, status;
    juce::ListBox list;
    juce::TextButton onButton { "Apagado" }, mixNowButton { "Mezclar ya" }, skipButton { "Saltar" },
                     removeButton { "Quitar" }, clearButton { "Vaciar" };
    juce::ComboBox fadeBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomixPanel)
};
}
