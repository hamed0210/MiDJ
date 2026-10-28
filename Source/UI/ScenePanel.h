#pragma once

#include "UI/Icons.h"
#include "UI/VideoWindow.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// Panel Escenas, como el de OBS: la lista (clic = pasar a esa escena con la transicion) y
// botones para anadir, borrar, renombrar y ordenar.
class ScenesPanel : public juce::Component,
                    private juce::ChangeListener,
                    private juce::ListBoxModel
{
public:
    explicit ScenesPanel (VideoStage& stage);
    ~ScenesPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();
    void rename (int sceneId);
    void editAuto (int sceneId);   // nombre y cambios automaticos de una escena
    void moveCurrent (int delta);
    void toggleMuted (int sceneId);   // silenciar la escena o devolverle el sonido

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void deleteKeyPressed (int) override { removeCurrent(); }
    void removeCurrent();

    VideoStage& stage;
    juce::Label title { {}, "Escenas" };
    juce::ListBox list;
    IconButton addButton { Icons::Glyph::plus, "Nueva escena" }, removeButton { Icons::Glyph::trash, "Borrar la escena al aire" },
               renameButton { Icons::Glyph::pencil, "Renombrar la escena al aire" },
               upButton { Icons::Glyph::up, "Subir la escena" }, downButton { Icons::Glyph::down, "Bajar la escena" };
};

// Panel Fuentes, como el de OBS: los elementos de la escena al aire (arriba el que se ve
// encima) con su ojo (visible) y su candado (no se mueve en la vista previa), y botones para
// anadir (dialogo Anadir fuente), quitar, propiedades y ordenar. Doble clic: propiedades.
class SourcesPanel : public juce::Component,
                     private juce::ChangeListener,
                     private juce::ListBoxModel
{
public:
    explicit SourcesPanel (VideoStage& stage);
    ~SourcesPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void refresh();
    SceneItem* itemAt (int row);
    void moveSelected (int delta);
    void showItemMenu (int itemId);
    void rename (int itemId);

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void deleteKeyPressed (int) override;
    void backgroundClicked (const juce::MouseEvent&) override { stage.selectItem (0); }

    VideoStage& stage;
    juce::Label title { {}, "Fuentes" };
    juce::ListBox list;
    IconButton addButton { Icons::Glyph::plus, "Anadir una fuente a la escena" },
               removeButton { Icons::Glyph::trash, "Quitar la fuente elegida (Supr)" },
               propertiesButton { Icons::Glyph::gear, "Propiedades de la fuente elegida" },
               upButton { Icons::Glyph::up, "Subir (se ve encima)" }, downButton { Icons::Glyph::down, "Bajar (se ve debajo)" };
};
}
