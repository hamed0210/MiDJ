#pragma once

#include "Engine/Effects.h"
#include "UI/DarkLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace midj
{
// Fila "FX" de un deck: tres ranuras con [nombre del efecto][v][mando].
//  - Clic en el nombre: enciende / apaga el efecto (se ilumina con el color del deck).
//  - Flecha: elegir otro efecto para esa ranura.
//  - Mando: intensidad. Doble clic lo devuelve al centro.
class EffectsRow : public juce::Component
{
public:
    EffectsRow (EffectRack& rack, juce::Colour accent);

    void resized() override;

private:
    struct Slot
    {
        juce::TextButton nameButton;
        juce::ArrowButton menuButton { "elegir", 0.25f, DarkLookAndFeel::textQuiet };   // flecha hacia abajo
        juce::Slider knob;
    };

    void updateSlot (int index);
    void showMenu (int index);

    EffectRack& rack;
    juce::Colour accent;
    juce::Label label;
    std::array<Slot, EffectRack::numSlots> slots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsRow)
};
}
