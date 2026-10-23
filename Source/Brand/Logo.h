#pragma once

#include <juce_graphics/juce_graphics.h>

namespace midj
{
// El logo de MiDJ dibujado (el mismo que Resources/Logo/icon.svg): un disco de vinilo con su
// brazo, y el nombre con "DJ" en naranja.
namespace Logo
{
    inline const juce::Colour accent { 0xffff5a36 };   // etiqueta del disco y "DJ"
    inline const juce::Colour cream  { 0xfff2efe8 };   // brazo y "Mi"

    // El disco con su brazo encajado en area (se centra en un cuadrado). Por debajo de 40 px se
    // dibuja simplificado, como icon-small.svg (sin surcos, etiqueta y brazo mas gruesos).
    void drawMark (juce::Graphics& g, juce::Rectangle<float> area, float opacity = 1.0f);
    // "MiDJ" con la fuente dada, alineado en area; devuelve su ancho.
    float drawWordmark (juce::Graphics& g, juce::Rectangle<float> area, const juce::Font& font,
                        juce::Justification justification, float opacity = 1.0f);
}
}
