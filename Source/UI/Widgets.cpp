#include "UI/Widgets.h"
#include "UI/DarkLookAndFeel.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace midj
{
// --- CentredSlider -----------------------------------------------------------------------------

CentredSlider::CentredSlider (double minValue, double maxValue, double snapRange)
    : centre ((minValue + maxValue) / 2.0), snap (snapRange)
{
    setSliderStyle (juce::Slider::LinearHorizontal);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRange (minValue, maxValue);
    setValue (centre, juce::dontSendNotification);
    setDoubleClickReturnValue (true, centre);
    setSliderSnapsToMousePosition (false);   // se arrastra desde donde esta, sin saltar al clic
}

double CentredSlider::snapValue (double attemptedValue, DragMode)
{
    return std::abs (attemptedValue - centre) < snap ? centre : attemptedValue;
}

void CentredSlider::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float left = bounds.getX() + thumbWidth / 2.0f, right = bounds.getRight() - thumbWidth / 2.0f;
    const float cy = bounds.getCentreY();
    auto xFor = [&] (double v) { return left + (float) valueToProportionOfLength (v) * (right - left); };
    const float x = xFor (getValue()), cx = xFor (centre);

    // Raya del centro (por detras del carril, sobresale arriba y abajo).
    g.setColour (isAtCentre() ? DarkLookAndFeel::good : DarkLookAndFeel::textFaint);
    g.fillRoundedRectangle (juce::Rectangle<float> (2.0f, juce::jmin (bounds.getHeight(), 22.0f)).withCentre ({ cx, cy }), 1.0f);

    const auto track = juce::Rectangle<float> (left, cy - 2.0f, right - left, 4.0f);
    g.setColour (DarkLookAndFeel::line.brighter (0.15f));
    g.fillRoundedRectangle (track, 2.0f);
    if (! fillColour.isTransparent() && std::abs (x - cx) > 0.5f)
    {
        g.setColour (fillColour);
        g.fillRect (juce::Rectangle<float> (juce::jmin (x, cx), cy - 2.0f, std::abs (x - cx), 4.0f));
    }

    const auto thumb = juce::Rectangle<float> ((float) thumbWidth, juce::jmin (bounds.getHeight() - 2.0f, 24.0f)).withCentre ({ x, cy });
    g.setColour (thumbColour.withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle (thumb, 4.0f);
    g.setColour (DarkLookAndFeel::background.withAlpha (0.7f));
    g.drawVerticalLine ((int) x, thumb.getY() + 4.0f, thumb.getBottom() - 4.0f);
}

// --- LevelMeter --------------------------------------------------------------------------------

void LevelMeter::setPeak (float peak)
{
    const float next = juce::jmax (peak, level * 0.86f);   // cae unos 20 dB por segundo a 30 Hz
    level = next < 0.003f ? 0.0f : next;
    // Solo se repinta si cambia algun segmento encendido (la mayor parte del tiempo, no).
    const int lit = (int) std::ceil (juce::jlimit (0.0f, (float) numSegments, (float) numSegments + juce::Decibels::gainToDecibels (level, -100.0f) / 3.0f));
    if (lit != litSegments)
    {
        litSegments = lit;
        repaint();
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float gap = 2.0f;
    const float h = (r.getHeight() - gap * (float) (numSegments - 1)) / (float) numSegments;
    // Escala en dB: el segmento de arriba es 0 dB y cada uno baja 3 dB (14 segmentos = -39 dB).
    const float db = juce::Decibels::gainToDecibels (level, -100.0f);
    const float lit = juce::jlimit (0.0f, (float) numSegments, (float) numSegments + db / 3.0f);
    for (int i = 0; i < numSegments; ++i)
    {
        const auto seg = juce::Rectangle<float> (r.getX(), r.getBottom() - (float) (i + 1) * h - (float) i * gap, r.getWidth(), h);
        const bool on = (float) i < lit;
        const auto colour = i >= numSegments - 2 ? DarkLookAndFeel::danger
                          : i >= numSegments - 5 ? juce::Colour (0xffe8c547) : DarkLookAndFeel::good;
        g.setColour (on ? colour : DarkLookAndFeel::selection);
        g.fillRoundedRectangle (seg, 1.0f);
    }
}

// --- LabelledKnob ------------------------------------------------------------------------------

LabelledKnob::LabelledKnob (const juce::String& n, double minValue, double maxValue, double defaultValue) : name (n)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (minValue, maxValue);
    slider.setValue (defaultValue, juce::dontSendNotification);
    slider.setDoubleClickReturnValue (true, defaultValue);
    slider.setMouseDragSensitivity (160);
    slider.addListener (this);
    addAndMakeVisible (slider);
}

void LabelledKnob::resized()
{
    auto r = getLocalBounds();
    if (showsText)
        r.removeFromBottom (formatValue ? 28 : 15);
    const int s = juce::jmin (r.getWidth(), r.getHeight(), 48);
    slider.setBounds (r.withSizeKeepingCentre (s, s));
}

void LabelledKnob::paint (juce::Graphics& g)
{
    if (! showsText)
        return;
    auto r = getLocalBounds().removeFromBottom (formatValue ? 28 : 15);
    g.setColour (DarkLookAndFeel::textQuiet);
    g.setFont (DarkLookAndFeel::sans (11.0f));
    g.drawText (name, r.removeFromTop (14), juce::Justification::centred, true);
    if (formatValue)
    {
        g.setColour (DarkLookAndFeel::text);
        g.setFont (DarkLookAndFeel::mono (11.0f));
        g.drawText (formatValue (slider.getValue()), r, juce::Justification::centred, true);
    }
}

// --- ResizeHandle ------------------------------------------------------------------------------

ResizeHandle::ResizeHandle (bool v) : vertical (v)
{
    setMouseCursor (vertical ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor);
    setTooltip (vertical ? "Arrastra para cambiar el alto (doble clic: alto original)"
                         : "Arrastra para cambiar el ancho (doble clic: ancho original)");
}

void ResizeHandle::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const auto grip = vertical ? r.withSizeKeepingCentre (juce::jmin (48.0f, r.getWidth()), 4.0f)
                               : r.withSizeKeepingCentre (4.0f, juce::jmin (48.0f, r.getHeight()));
    g.setColour (dragging ? DarkLookAndFeel::accentA : isMouseOver() ? DarkLookAndFeel::textQuiet : DarkLookAndFeel::line.brighter (0.15f));
    g.fillRoundedRectangle (grip, 2.0f);
}

void ResizeHandle::mouseDown (const juce::MouseEvent&)
{
    dragging = true;
    if (onStart)
        onStart();
    repaint();
}

void ResizeHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (onDrag)
        onDrag (vertical ? e.getDistanceFromDragStartY() : e.getDistanceFromDragStartX());
}
}
