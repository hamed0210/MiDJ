#pragma once

#include "Video/Scenes.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// Ventana "Nueva transicion personalizada" (o "Modificar transicion"): nombre, tipo (stinger,
// barrido con imagen o a color) y sus ajustes, con "Probar en el programa" (la hace sin cambiar
// de escena) y "Anadir transicion" / "Guardar cambios".
class TransitionDialog : public juce::Component,
                         private juce::ChangeListener
{
public:
    TransitionDialog();
    ~TransitionDialog() override;

    std::function<void (const CustomTransition&)> onTry, onSave;
    // Abre la ventana abajo a la izquierda de la principal, para que se vea la salida de video al
    // probar (la ventana principal espera mientras esta abierta). Con existing, para modificarla
    // (lo guardado lleva su id).
    static void show (juce::Component* parent, std::function<void (const CustomTransition&)> tryIt,
                      std::function<void (const CustomTransition&)> save, const CustomTransition* existing = nullptr);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class KindCard;
    CustomTransition current() const;
    void load (const CustomTransition& existing);   // para modificarla
    void setKind (CustomTransitionKind kind);
    void chooseFile();
    // El archivo elegido (stinger: su duracion y si lleva mate; converted: es un video con mate de MiDJ).
    void useFile (const juce::File& chosen, bool stinger, bool converted);
    void setColour (juce::Colour c);
    void update();
    void close();
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;   // el selector de color

    CustomTransitionKind kind = CustomTransitionKind::stinger;
    int editingId = 0;   // 0 = nueva
    juce::File file;
    juce::Colour colour { juce::Colours::black };

    juce::TextEditor nameEditor, fileEditor;
    std::array<std::unique_ptr<KindCard>, 3> kinds;
    juce::TextButton browseButton { "Examinar..." };
    juce::ToggleButton matteToggle { "Video con mate (color a la izquierda, transparencia a la derecha)" };
    juce::Slider cutSlider, durationSlider, softnessSlider;
    std::array<juce::TextButton, 6> swatches;
    juce::TextButton otherColourButton { "Otro..." };
    juce::TextButton tryButton { "Probar en el programa" }, cancelButton { "Cancelar" }, saveButton { "Anadir transicion" };
    juce::Rectangle<int> nameLabel, kindLabel, fileLabel, cutLabel, durationLabel, softnessLabel, colourLabel, noteArea;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransitionDialog)
};
}
