#pragma once

#include "Engine/Effects.h"
#include "UI/DarkLookAndFeel.h"
#include "UI/Icons.h"
#include "UI/MidiLearnOverlay.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace midj
{
// Los tres efectos de un deck, cada uno en su ranura: el nombre del efecto es el boton que lo
// enciende (lleno del color del deck cuando suena), la flecha elige otro efecto y el mando es la
// cantidad (doble clic: al centro).
class EffectsRow : public juce::Component,
                   private juce::Timer
{
public:
    EffectsRow (EffectRack& rack, juce::Colour accent);

    void paint (juce::Graphics& g) override;
    void resized() override;
    // Controles asignables por MIDI: encender cada ranura y su mando. prefix: "deck1."...
    void addLearnTargets (std::vector<LearnTarget>& out, const juce::String& prefix);

private:
    void timerCallback() override;   // sigue los cambios que lleguen por MIDI

    struct Slot
    {
        juce::TextButton nameButton;
        IconButton menuButton { Icons::Glyph::down, "Elegir otro efecto" };
        juce::Slider knob;
        juce::Rectangle<int> area;
    };

    void updateSlot (int index);
    void showMenu (int index);

    EffectRack& rack;
    juce::Colour accent;
    std::array<Slot, EffectRack::numSlots> slots;
    std::array<int, EffectRack::numSlots> lastOn { -1, -1, -1 };   // para no repintar si no cambia

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsRow)
};
}
