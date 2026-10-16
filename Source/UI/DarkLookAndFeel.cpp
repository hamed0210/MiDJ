#include "UI/DarkLookAndFeel.h"

namespace midj
{
DarkLookAndFeel::DarkLookAndFeel()
{
    setColourScheme (LookAndFeel_V4::getMidnightColourScheme());
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::buttonColourId, panel.brighter (0.15f));
    setColour (juce::TextButton::textColourOffId, text);
}

// Fader plano: carril fino y un tirador rectangular, como en una mesa de DJ.
void DarkLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float, float,
                                        juce::Slider::SliderStyle style, juce::Slider&)
{
    const bool horizontal = style == juce::Slider::LinearHorizontal;
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);

    g.setColour (textQuiet.withAlpha (0.4f));
    if (horizontal)
        g.fillRoundedRectangle (bounds.withSizeKeepingCentre (bounds.getWidth(), 4.0f), 2.0f);
    else
        g.fillRoundedRectangle (bounds.withSizeKeepingCentre (4.0f, bounds.getHeight()), 2.0f);

    const auto thumb = horizontal
        ? juce::Rectangle<float> (18.0f, bounds.getHeight() * 0.8f).withCentre ({ sliderPos, bounds.getCentreY() })
        : juce::Rectangle<float> (bounds.getWidth() * 0.6f, 18.0f).withCentre ({ bounds.getCentreX(), sliderPos });
    g.setColour (text);
    g.fillRoundedRectangle (thumb, 3.0f);
    g.setColour (background);
    if (horizontal)
        g.drawVerticalLine ((int) sliderPos, thumb.getY() + 3.0f, thumb.getBottom() - 3.0f);
    else
        g.drawHorizontalLine ((int) sliderPos, thumb.getX() + 3.0f, thumb.getRight() - 3.0f);
}

// Mando de mezclador: el arco de color sale de las 12 (posicion neutra) hacia el valor,
// asi un EQ o un filtro en el centro no muestra nada encendido.
void DarkLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float arcRadius = radius - 3.0f;
    const float centreAngle = (startAngle + endAngle) / 2.0f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    auto arc = [&] (float from, float to, juce::Colour colour)
    {
        juce::Path p;
        p.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, from, to, true);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    arc (startAngle, endAngle, background);
    if (std::abs (angle - centreAngle) > 0.01f)
        arc (juce::jmin (centreAngle, angle), juce::jmax (centreAngle, angle),
             slider.findColour (juce::Slider::rotarySliderFillColourId));

    // Cuerpo del mando y raya indicadora.
    const float bodyRadius = arcRadius - 5.0f;
    g.setColour (panel.brighter (0.25f));
    g.fillEllipse (juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre));
    const auto tip = centre.getPointOnCircumference (bodyRadius - 2.0f, angle);
    const auto base = centre.getPointOnCircumference (bodyRadius * 0.25f, angle);
    g.setColour (text);
    g.drawLine ({ base, tip }, 2.5f);
}
}
