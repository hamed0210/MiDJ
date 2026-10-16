#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// Estilo oscuro comun a toda la aplicacion. Los colores se definen aqui una sola vez.
class DarkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DarkLookAndFeel();

    static inline const juce::Colour background { 0xff15171c };
    static inline const juce::Colour panel      { 0xff1f2229 };
    static inline const juce::Colour accentA    { 0xff2f9be0 };   // deck A, azul
    static inline const juce::Colour accentB    { 0xffe0453a };   // deck B, rojo
    static inline const juce::Colour text       { 0xffe6e8ec };
    static inline const juce::Colour textQuiet  { 0xff8b919c };

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
};
}
