#pragma once

#include "Video/Scenes.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// Iconos dibujados (sin imagenes ni fuentes de simbolos): botones de los paneles de escenas y
// fuentes, tipos de fuente y archivos de audio o video en la biblioteca.
namespace Icons
{
    enum class Glyph { plus, minus, trash, gear, up, down, pencil, eye, eyeOff, lock, unlock,
                       play, pause, stop, restart, film, note,
                       mix, skip, folder, folderPlus, sync, drive, computer, star, chevron, close, monitor, sliders, headphones,
                       speaker, speakerOff, phone };

    void draw (juce::Graphics& g, juce::Rectangle<float> area, Glyph glyph, juce::Colour colour);
    // El icono de un tipo de fuente (como los del dialogo Anadir fuente de OBS).
    void drawSource (juce::Graphics& g, juce::Rectangle<float> area, SourceType type, juce::Colour colour);
    // Audio o video (para la lista de canciones).
    void drawFileType (juce::Graphics& g, juce::Rectangle<float> area, bool video);
}

// Boton pequeno con un icono (como los de los paneles de OBS).
class IconButton : public juce::Button
{
public:
    IconButton (Icons::Glyph glyph, const juce::String& tooltip);
    void setGlyph (Icons::Glyph g) { glyph = g; repaint(); }
    // Con borde (botones de barra) y, si se da, lleno de un color con el icono oscuro (Mezclar ya).
    void setBordered (bool b, juce::Colour fillColour = {}) { bordered = b; fill = fillColour; repaint(); }
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

private:
    Icons::Glyph glyph;
    bool bordered = false;
    juce::Colour fill;
};
}
