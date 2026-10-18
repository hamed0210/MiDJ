#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// Estilo oscuro comun a toda la aplicacion (el del diseno "Escenario"). Los colores se definen
// aqui una sola vez: fondo casi negro, paneles grises con esquinas redondeadas, el deck A en
// azul y el B en ambar (se distinguen tambien por la luminosidad, no solo por el tono).
class DarkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DarkLookAndFeel();

    static inline const juce::Colour background { 0xff0a0a0b };   // fondo de la ventana
    static inline const juce::Colour panel      { 0xff141416 };   // tarjetas (decks, paneles)
    static inline const juce::Colour raised     { 0xff1c1c20 };   // dentro de una tarjeta (pads, ranuras)
    static inline const juce::Colour selection  { 0xff26262b };   // fila o pestana elegida
    static inline const juce::Colour line       { 0xff2a2a30 };   // bordes
    static inline const juce::Colour accentA    { 0xff3ea6ff };   // deck A, azul
    static inline const juce::Colour accentB    { 0xffffb020 };   // deck B, ambar
    static inline const juce::Colour good       { 0xff3ddc97 };   // verde: centro, al aire, encendido
    static inline const juce::Colour danger     { 0xffff5d5d };   // rojo: picos del vumetro
    static inline const juce::Colour custom     { 0xff9b8cff };   // morado: transiciones personalizadas
    static inline const juce::Colour text       { 0xfff2f2f3 };
    static inline const juce::Colour textQuiet  { 0xffa0a0a8 };
    static inline const juce::Colour textFaint  { 0xff6e6e78 };

    static juce::Colour deckColour (int deckIndex) { return deckIndex == 0 ? accentA : accentB; }
    // Numeros (BPM, tiempos, porcentajes): monoespaciada para que no bailen al cambiar.
    static juce::FontOptions mono (float height, bool bold = false);
    static juce::FontOptions sans (float height, bool bold = false);

    // Marca para un boton que se pinta lleno con su color (Play, ON de un efecto...).
    static constexpr const char* filledProperty = "midjFilled";

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawStretchableLayoutResizerBar (juce::Graphics&, int w, int h, bool isVerticalBar,
                                          bool isMouseOver, bool isMouseDragging) override;
};
}
