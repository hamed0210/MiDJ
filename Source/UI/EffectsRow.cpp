#include "UI/EffectsRow.h"
#include "UI/DarkLookAndFeel.h"

namespace midj
{
EffectsRow::EffectsRow (EffectRack& r, juce::Colour c) : rack (r), accent (c)
{
    label.setText ("FX", juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centredRight);
    label.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
    addAndMakeVisible (label);

    for (int i = 0; i < EffectRack::numSlots; ++i)
    {
        auto& s = slots[(size_t) i];
        s.nameButton.onClick = [this, i]
        {
            rack.setOn (i, ! rack.isOn (i));
            updateSlot (i);
        };
        s.nameButton.setTooltip ("Encender / apagar el efecto");
        s.menuButton.onClick = [this, i] { showMenu (i); };
        s.menuButton.setTooltip ("Elegir efecto");

        s.knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.knob.setRange (0.0, 1.0);
        s.knob.setValue (rack.getAmount (i), juce::dontSendNotification);
        s.knob.setDoubleClickReturnValue (true, 0.5);
        s.knob.setColour (juce::Slider::rotarySliderFillColourId, accent);
        s.knob.onValueChange = [this, i] { rack.setAmount (i, (float) slots[(size_t) i].knob.getValue()); };
        s.knob.setTooltip ("Intensidad del efecto");

        for (auto* comp : std::initializer_list<juce::Component*> { &s.nameButton, &s.menuButton, &s.knob })
            addAndMakeVisible (comp);
        updateSlot (i);
    }
}

void EffectsRow::updateSlot (int index)
{
    auto& s = slots[(size_t) index];
    s.nameButton.setButtonText (EffectRack::getEffectNames()[rack.getEffect (index)]);
    const bool on = rack.isOn (index);
    s.nameButton.setColour (juce::TextButton::buttonColourId, on ? accent.withAlpha (0.75f) : DarkLookAndFeel::panel.brighter (0.15f));
    s.nameButton.setColour (juce::TextButton::textColourOffId, on ? juce::Colours::white : DarkLookAndFeel::text);
}

void EffectsRow::showMenu (int index)
{
    juce::PopupMenu menu;
    const auto names = EffectRack::getEffectNames();
    for (int e = 0; e < names.size(); ++e)
        menu.addItem (e + 1, names[e], true, rack.getEffect (index) == e);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slots[(size_t) index].menuButton),
                        [this, index] (int result)
                        {
                            if (result > 0)
                            {
                                rack.setEffect (index, result - 1);
                                updateSlot (index);
                            }
                        });
}

void EffectsRow::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromLeft (44));
    area.removeFromLeft (6);
    const int slotWidth = area.getWidth() / EffectRack::numSlots;
    for (auto& s : slots)
    {
        auto slot = area.removeFromLeft (slotWidth).withTrimmedRight (8);
        s.knob.setBounds (slot.removeFromRight (slot.getHeight() + 4));
        s.menuButton.setBounds (slot.removeFromRight (18).reduced (2, 9));
        s.nameButton.setBounds (slot.reduced (0, 2).withTrimmedRight (1));
    }
}
}
