#include "UI/DarkLookAndFeel.h"

namespace midj
{
DarkLookAndFeel::DarkLookAndFeel()
{
    setColourScheme (LookAndFeel_V4::getMidnightColourScheme());
    setDefaultSansSerifTypefaceName ("Segoe UI");
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::buttonOnColourId, selection);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, text);
    setColour (juce::ComboBox::backgroundColourId, background);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textQuiet);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff18181b));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, selection);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, textQuiet);   // titulos de seccion (por defecto salen negros)
    setColour (juce::TextEditor::backgroundColourId, background);
    setColour (juce::TextEditor::outlineColourId, line);
    setColour (juce::TextEditor::focusedOutlineColourId, accentA);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ScrollBar::thumbColourId, line.brighter (0.2f));
    setColour (juce::Slider::trackColourId, text);
    setColour (juce::Slider::rotarySliderFillColourId, accentA);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff18181b));
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, line);
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff18181b));
    setColour (juce::CaretComponent::caretColourId, text);
}

juce::FontOptions DarkLookAndFeel::mono (float height, bool bold)
{
    return juce::FontOptions ("Consolas", height, bold ? juce::Font::bold : juce::Font::plain);
}

juce::FontOptions DarkLookAndFeel::sans (float height, bool bold)
{
    return juce::FontOptions ("Segoe UI", height, bold ? juce::Font::bold : juce::Font::plain);
}

// Botones planos con esquinas redondeadas: con borde fino si no tienen color propio, llenos de
// su color si son "llenos" (Play, ON) o estan encendidos.
void DarkLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                            bool highlighted, bool down)
{
    auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = juce::jmin (8.0f, r.getHeight() * 0.3f);
    auto fill = backgroundColour;
    if (button.getToggleState() && fill.isTransparent())
        fill = findColour (juce::TextButton::buttonOnColourId);
    if (! fill.isTransparent())
    {
        g.setColour (fill.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f));
        g.fillRoundedRectangle (r, corner);
    }
    const bool filled = (bool) button.getProperties().getWithDefault (filledProperty, false);
    if (! filled)
    {
        g.setColour (button.findColour (juce::ComboBox::outlineColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        g.drawRoundedRectangle (r, corner, 1.0f);
    }
    if (highlighted || down)
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.12f : 0.05f));
        g.fillRoundedRectangle (r, corner);
    }
}

juce::Font DarkLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (sans (juce::jlimit (11.0f, 14.0f, (float) buttonHeight * 0.4f), true));
}

void DarkLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    const auto colour = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    g.setColour (colour.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1, 0.8f);
}

// Fader plano: carril fino y un tirador claro. Vertical (volumenes): de abajo al valor en el
// color del carril; horizontal: carril neutro.
void DarkLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float, float,
                                        juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool horizontal = style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar;
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto fillColour = slider.findColour (juce::Slider::trackColourId);

    if (horizontal)
    {
        const auto track = bounds.withSizeKeepingCentre (bounds.getWidth(), 4.0f);
        g.setColour (line);
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (fillColour.withAlpha (0.85f));
        g.fillRoundedRectangle (track.withRight (sliderPos), 2.0f);
        const auto thumb = juce::Rectangle<float> (14.0f, juce::jmin (bounds.getHeight(), 20.0f)).withCentre ({ sliderPos, bounds.getCentreY() });
        g.setColour (text);
        g.fillRoundedRectangle (thumb, 4.0f);
        return;
    }
    const auto track = bounds.withSizeKeepingCentre (6.0f, bounds.getHeight());
    g.setColour (line);
    g.fillRoundedRectangle (track, 3.0f);
    g.setColour (fillColour);
    g.fillRoundedRectangle (track.withTop (sliderPos), 3.0f);
    const auto thumb = juce::Rectangle<float> (juce::jmin (bounds.getWidth(), 26.0f), 12.0f).withCentre ({ bounds.getCentreX(), sliderPos });
    g.setColour (text);
    g.fillRoundedRectangle (thumb, 3.0f);
    g.setColour (background);
    g.drawHorizontalLine ((int) sliderPos, thumb.getX() + 4.0f, thumb.getRight() - 4.0f);
}

// Mando: anillo oscuro y una raya que apunta al valor. Si no esta en su posicion neutra
// (la de doble clic), anillo y raya se pintan del color del mando.
void DarkLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (1.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const bool changed = std::abs (slider.getValue() - slider.getDoubleClickReturnValue()) > 1.0e-4;
    const auto colour = slider.findColour (juce::Slider::rotarySliderFillColourId);

    g.setColour (background);
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    g.setColour (changed ? colour : line);
    g.drawEllipse (juce::Rectangle<float> (radius * 2.0f - 2.0f, radius * 2.0f - 2.0f).withCentre (centre), 2.0f);
    const auto tip = centre.getPointOnCircumference (radius - 4.0f, angle);
    const auto base = centre.getPointOnCircumference (radius * 0.15f, angle);
    g.setColour (changed ? colour : text);
    g.drawLine ({ base, tip }, 2.0f);
}

void DarkLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                    int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) width - 12.0f, ay = (float) height * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.0f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font DarkLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return juce::Font (sans (juce::jlimit (11.0f, 14.0f, (float) box.getHeight() * 0.45f)));
}

// Interruptor (como los de los ajustes del diseno): texto a la izquierda y el interruptor a la derecha.
void DarkLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    auto r = button.getLocalBounds().toFloat();
    const auto sw = r.removeFromRight (40.0f).withSizeKeepingCentre (40.0f, 22.0f);
    const bool on = button.getToggleState();
    g.setColour (on ? accentA : line.brighter (0.1f));
    g.fillRoundedRectangle (sw, 11.0f);
    g.setColour (juce::Colours::white);
    g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ on ? sw.getRight() - 11.0f : sw.getX() + 11.0f, sw.getCentreY() }));
    if (highlighted)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (sw, 11.0f);
    }
    g.setColour (text.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.setFont (sans (14.0f, true));
    g.drawFittedText (button.getButtonText(), r.toNearestInt().withTrimmedRight (8), juce::Justification::centredLeft, 2);
}

void DarkLookAndFeel::drawStretchableLayoutResizerBar (juce::Graphics& g, int w, int h, bool isVerticalBar,
                                                       bool isMouseOver, bool isMouseDragging)
{
    // Una barrita en el centro que se ilumina al pasar o arrastrar.
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
    const auto grip = isVerticalBar ? r.withSizeKeepingCentre (4.0f, juce::jmin (48.0f, r.getHeight()))
                                    : r.withSizeKeepingCentre (juce::jmin (48.0f, r.getWidth()), 4.0f);
    g.setColour (isMouseDragging ? accentA : isMouseOver ? textQuiet : line.brighter (0.15f));
    g.fillRoundedRectangle (grip, 2.0f);
}
}
