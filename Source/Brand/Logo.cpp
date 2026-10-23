#include "Brand/Logo.h"

namespace midj::Logo
{
void drawMark (juce::Graphics& g, juce::Rectangle<float> area, float opacity)
{
    // Coordenadas del dibujo original: un cuadrado de 220 con el disco en el centro.
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    const auto box = juce::Rectangle<float> (size, size).withCentre (area.getCentre());
    const float s = size / 220.0f;
    auto at = [&] (float x, float y) { return juce::Point<float> (box.getX() + x * s, box.getY() + y * s); };
    auto circle = [&] (float r, juce::Colour c)
    {
        g.setColour (c.withMultipliedAlpha (opacity));
        g.fillEllipse (juce::Rectangle<float> (2.0f * r * s, 2.0f * r * s).withCentre (at (110.0f, 110.0f)));
    };
    const bool small = size < 40.0f;
    circle (100.0f, juce::Colour (small ? 0xff2a2d37 : 0xff1b1d24));
    if (! small)
    {
        g.setColour (juce::Colour (0xff2e313c).withMultipliedAlpha (opacity));
        for (float r : { 86.0f, 70.0f, 54.0f })
            g.drawEllipse (juce::Rectangle<float> (2.0f * r * s, 2.0f * r * s).withCentre (at (110.0f, 110.0f)), 2.0f * s);
    }
    circle (small ? 48.0f : 36.0f, accent);
    circle (small ? 12.0f : 6.0f, juce::Colour (0xff0e0f13));

    g.setColour (cream.withMultipliedAlpha (opacity));
    juce::Path arm;
    arm.startNewSubPath (small ? at (190.0f, 28.0f) : at (188.0f, 30.0f));
    arm.lineTo (small ? at (162.0f, 100.0f) : at (164.0f, 98.0f));
    g.strokePath (arm, juce::PathStrokeType ((small ? 22.0f : 9.0f) * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (! small)
        g.fillEllipse (juce::Rectangle<float> (26.0f * s, 26.0f * s).withCentre (at (188.0f, 30.0f)));
}

float drawWordmark (juce::Graphics& g, juce::Rectangle<float> area, const juce::Font& font,
                    juce::Justification justification, float opacity)
{
    const float mi = juce::GlyphArrangement::getStringWidth (font, "Mi");
    const float width = mi + juce::GlyphArrangement::getStringWidth (font, "DJ");
    const auto text = justification.appliedToRectangle (juce::Rectangle<float> (width, area.getHeight()), area);
    g.setFont (font);
    g.setColour (cream.withMultipliedAlpha (opacity));
    g.drawText ("Mi", text.withWidth (mi), juce::Justification::centredLeft, false);
    g.setColour (accent.withMultipliedAlpha (opacity));
    g.drawText ("DJ", text.withTrimmedLeft (mi), juce::Justification::centredLeft, false);
    return width;
}
}
