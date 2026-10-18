#include "UI/EffectsRow.h"
#include "UI/DarkLookAndFeel.h"

namespace midj
{
EffectsRow::EffectsRow (EffectRack& r, juce::Colour c) : rack (r), accent (c)
{
    for (int i = 0; i < EffectRack::numSlots; ++i)
    {
        auto& s = slots[(size_t) i];
        s.nameButton.onClick = [this, i]
        {
            rack.setOn (i, ! rack.isOn (i));
            updateSlot (i);
        };
        s.nameButton.setTooltip ("Encender / apagar el efecto (la flecha elige otro)");
        s.nameButton.getProperties().set (DarkLookAndFeel::filledProperty, true);
        s.menuButton.onClick = [this, i] { showMenu (i); };

        s.knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.knob.setRange (0.0, 1.0);
        s.knob.setValue (rack.getAmount (i), juce::dontSendNotification);
        s.knob.setDoubleClickReturnValue (true, 0.5);
        s.knob.setColour (juce::Slider::rotarySliderFillColourId, accent);
        s.knob.onValueChange = [this, i] { rack.setAmount (i, (float) slots[(size_t) i].knob.getValue()); };
        s.knob.setTooltip ("Cantidad del efecto (doble clic: al centro)");

        for (auto* comp : std::initializer_list<juce::Component*> { &s.nameButton, &s.menuButton, &s.knob })
            addAndMakeVisible (comp);
        updateSlot (i);
    }
    startTimerHz (10);
}

void EffectsRow::addLearnTargets (std::vector<LearnTarget>& out, const juce::String& prefix)
{
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const auto fx = prefix + "fx" + juce::String ((int) i + 1);
        out.push_back ({ &slots[i].nameButton, fx });
        out.push_back ({ &slots[i].knob, fx + "_amount" });
    }
}

void EffectsRow::timerCallback()
{
    for (int i = 0; i < EffectRack::numSlots; ++i)
    {
        updateSlot (i);
        auto& knob = slots[(size_t) i].knob;
        if (! knob.isMouseButtonDown())
            knob.setValue (rack.getAmount (i), juce::dontSendNotification);
    }
}

void EffectsRow::updateSlot (int index)
{
    auto& s = slots[(size_t) index];
    s.nameButton.setButtonText (EffectRack::getEffectNames()[rack.getEffect (index)]);
    const bool on = rack.isOn (index);
    if (lastOn[(size_t) index] == (int) on)
        return;
    lastOn[(size_t) index] = (int) on;
    s.nameButton.setColour (juce::TextButton::buttonColourId, on ? accent : DarkLookAndFeel::background);
    s.nameButton.setColour (juce::TextButton::textColourOffId, on ? DarkLookAndFeel::background : DarkLookAndFeel::text);
    repaint();
}

void EffectsRow::showMenu (int index)
{
    juce::PopupMenu menu;
    const auto names = EffectRack::getEffectNames();
    for (int e = 0; e < names.size(); ++e)
        menu.addItem (e + 1, names[e], true, rack.getEffect (index) == e);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slots[(size_t) index].nameButton),
                        [this, index] (int result)
                        {
                            if (result > 0)
                            {
                                rack.setEffect (index, result - 1);
                                updateSlot (index);
                            }
                        });
}

void EffectsRow::paint (juce::Graphics& g)
{
    for (int i = 0; i < EffectRack::numSlots; ++i)
    {
        const auto r = slots[(size_t) i].area.toFloat();
        g.setColour (DarkLookAndFeel::raised);
        g.fillRoundedRectangle (r, 10.0f);
        if (rack.isOn (i))
        {
            g.setColour (accent);
            g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
        }
    }
}

void EffectsRow::resized()
{
    auto area = getLocalBounds();
    const int gap = 6;
    const int slotWidth = (area.getWidth() - gap * (EffectRack::numSlots - 1)) / EffectRack::numSlots;
    for (auto& s : slots)
    {
        s.area = area.removeFromLeft (slotWidth);
        area.removeFromLeft (gap);
        auto r = s.area.reduced (5);
        const int knob = juce::jmin (r.getHeight(), 30);
        s.knob.setBounds (r.removeFromRight (knob).withSizeKeepingCentre (knob, knob));
        r.removeFromRight (2);
        s.menuButton.setBounds (r.removeFromRight (16));
        r.removeFromRight (2);
        s.nameButton.setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 28)));
    }
}
}
