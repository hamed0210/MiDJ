#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace midj
{
// Fader horizontal con su centro marcado (pitch y crossfader): una raya en el medio que se pone
// verde cuando el valor esta justo en el centro, y un "clic" que lo deja en el centro al pasar
// cerca (como el punto de un fader real). Doble clic: al centro.
class CentredSlider : public juce::Slider
{
public:
    // snapRange: distancia al centro (en unidades del valor) en la que se queda en el centro.
    CentredSlider (double minValue, double maxValue, double snapRange);

    void setThumbColour (juce::Colour c) { thumbColour = c; repaint(); }
    // Rellenar del centro al valor (el pitch) con este color; transparente = sin relleno.
    void setFillColour (juce::Colour c) { fillColour = c; repaint(); }
    bool isAtCentre() const { return std::abs (getValue() - centre) < 1.0e-6; }
    double getCentre() const { return centre; }

    void paint (juce::Graphics& g) override;
    double snapValue (double attemptedValue, DragMode dragMode) override;

    static constexpr int thumbWidth = 16;

private:
    double centre, snap;
    juce::Colour thumbColour { 0xfff2f2f3 }, fillColour;
};

// Vumetro vertical por segmentos (verde, amarillo y rojo arriba). setPeak() con el pico leido;
// cae poco a poco si no llega nada mas alto.
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (int segments = 14) : numSegments (segments) {}
    void setPeak (float peak);   // 0..1 (por encima de 1 se enciende el rojo)
    void paint (juce::Graphics& g) override;

private:
    int numSegments;
    float level = 0.0f;
};

// Mando con su nombre y su valor debajo (EQ, filtro, master, efectos).
class LabelledKnob : public juce::Component, private juce::Slider::Listener
{
public:
    LabelledKnob (const juce::String& name, double minValue, double maxValue, double defaultValue);

    juce::Slider slider;
    std::function<juce::String (double)> formatValue;   // el texto del valor; sin el, no se muestra

    void paint (juce::Graphics& g) override;
    void resized() override;
    void setShowsText (bool shows) { showsText = shows; resized(); repaint(); }
    ~LabelledKnob() override { slider.removeListener (this); }

private:
    juce::String name;
    bool showsText = true;
    void sliderValueChanged (juce::Slider*) override { repaint(); }   // el texto del valor
};

// Barrita para cambiar el ancho de un panel arrastrando (o con las flechas si tiene el foco).
// onDrag recibe los pixeles movidos desde que se pulso; onReset, al hacer doble clic.
class ResizeHandle : public juce::Component, public juce::SettableTooltipClient
{
public:
    ResizeHandle();
    std::function<void()> onStart, onReset;
    std::function<void (int deltaX)> onDrag;

    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent&) override    { dragging = false; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onReset) onReset(); }

private:
    bool dragging = false;
};
}
