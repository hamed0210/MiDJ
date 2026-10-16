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
}
