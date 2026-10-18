#pragma once

#include "Video/Scenes.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// Ventana "Nueva transicion personalizada": nombre, tipo (stinger, barrido con imagen o a
// color) y sus ajustes, con "Probar en el programa" (la hace sin cambiar de escena) y
// "Anadir transicion".
class TransitionDialog : public juce::Component,
                         private juce::ChangeListener
{
public:
    TransitionDialog();
    ~TransitionDialog() override;

    std::function<void (const CustomTransition&)> onTry, onSave;
    // Abre la ventana encima de la principal (la ventana principal espera mientras esta abierta).
    static void show (juce::Component* parent, std::function<void (const CustomTransition&)> tryIt,
                      std::function<void (const CustomTransition&)> save);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class KindCard;
    CustomTransition current() const;
    void setKind (CustomTransitionKind kind);
    void chooseFile();
    void setColour (juce::Colour c);
    void update();
    void close();
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;   // el selector de color

    CustomTransitionKind kind = CustomTransitionKind::stinger;
    juce::File file;
    juce::Colour colour { juce::Colours::black };

    juce::TextEditor nameEditor, fileEditor;
    std::array<std::unique_ptr<KindCard>, 3> kinds;
    juce::TextButton browseButton { "Examinar..." };
    juce::Slider cutSlider, durationSlider, softnessSlider;
    std::array<juce::TextButton, 6> swatches;
    juce::TextButton otherColourButton { "Otro..." };
    juce::TextButton tryButton { "Probar en el programa" }, cancelButton { "Cancelar" }, saveButton { "Anadir transicion" };
    juce::Rectangle<int> nameLabel, kindLabel, fileLabel, cutLabel, durationLabel, softnessLabel, colourLabel, noteArea;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransitionDialog)
};
}
