#pragma once

#include "Video/VideoProgram.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// Panel de escenas de la ventana de video, como los paneles Escenas, Fuentes y Transiciones de
// OBS: elegir la escena al aire, sus elementos (de arriba = el que se ve encima) y las
// propiedades del elemento elegido. Edita la coleccion y avisa con onChanged.
class ScenePanel : public juce::Component, private juce::Timer
{
public:
    ScenePanel (SceneCollection& scenes, const VideoProgram& program, int programWidth, int programHeight);
    ~ScenePanel() override;

    std::function<void()> onChanged;                  // cambio en las escenas (se publica y se guarda)
    std::function<void (int sceneId)> onSceneChosen;  // pasar a esa escena (con la transicion)
    std::function<void (int itemId)> onItemSelected;  // 0 = ninguno

    void refresh();                 // volver a leer la coleccion (p. ej. tras arrastrar en la vista previa)
    void setSelectedItem (int itemId);
    int getSelectedItem() const { return selectedItem; }
    Scene* currentScene();

    void deleteSelectedItem();
    // Colocaciones rapidas del elemento elegido.
    void fitSelectedToScreen();
    void resetSelected();

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class SceneListModel;
    class ItemListModel;

    SceneItem* selected();
    void changed();
    void updateProperties();
    void addItem (SourceType type);
    void moveSelected (int delta);
    void chooseColour();
    void chooseImage();
    void fillCaptureTargets();
    void chooseCaptureTarget();
    void timerCallback() override;   // el estado de la captura elegida
    juce::Slider& addSlider (juce::Slider& s, double min, double max, double step, std::function<void (SceneItem&, float)> apply);

    SceneCollection& scenes;
    const VideoProgram& program;
    const int width, height;
    int selectedItem = 0;
    bool updating = false;

    std::unique_ptr<SceneListModel> sceneModel;
    std::unique_ptr<ItemListModel> itemModel;
    juce::Label scenesLabel { {}, "ESCENAS" }, itemsLabel { {}, "FUENTES" }, propsLabel { {}, "PROPIEDADES" };
    juce::ListBox sceneList, itemList;
    juce::TextButton addScene { "+" }, removeScene { "-" }, renameScene { "Renombrar" };
    juce::TextButton addItemButton { "+" }, removeItem { "-" }, moveUp { "Subir" }, moveDown { "Bajar" };
    juce::ComboBox transitionType;
    juce::Slider transitionMs;

    // Propiedades
    juce::TextEditor textEditor;
    juce::Slider fontSize, opacity, rotation, cropLeft, cropTop, cropRight, cropBottom;
    juce::Label fontSizeLabel { {}, "Tamano" }, opacityLabel { {}, "Opacidad" }, rotationLabel { {}, "Giro" },
                cropLabel { {}, "Recorte (izq, arr, der, abajo)" };
    juce::ToggleButton boldToggle { "Negrita" }, shadowToggle { "Sombra" };
    juce::TextButton colourButton { "Color..." }, fileButton { "Imagen..." }, fitButton { "Ajustar a pantalla" },
                     resetButton { "Restablecer" };
    juce::ComboBox captureTarget;
    juce::TextButton refreshTargets { "Actualizar" };
    juce::ToggleButton cursorToggle { "Capturar el puntero" }, clientToggle { "Solo el contenido (sin bordes)" };
    juce::Label captureStatus;
    std::vector<ScreenCapture::Monitor> monitorList;
    std::vector<ScreenCapture::Window> windowList;
    std::unique_ptr<juce::FileChooser> chooser;
};
}
