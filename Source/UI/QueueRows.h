#pragma once

#include "UI/DarkLookAndFeel.h"
#include "UI/Icons.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj::QueueRows
{
// Filas de las listas del automix (decks y YouTube): a la derecha subir, bajar y quitar, y se
// pueden arrastrar con el raton a otro sitio de la lista (una raya marca donde caen).

enum class Hit { none, up, down, remove };

// Dibuja subir / bajar / quitar a la derecha de 'r' (ya sin el margen) y le quita ese sitio.
inline void drawButtons (juce::Graphics& g, juce::Rectangle<float>& r, int row, int numRows)
{
    const auto quiet = DarkLookAndFeel::textQuiet;
    const auto off = quiet.withMultipliedAlpha (0.3f);
    Icons::draw (g, r.removeFromRight (16.0f).withSizeKeepingCentre (12.0f, 12.0f), Icons::Glyph::close, quiet);
    r.removeFromRight (6.0f);
    Icons::draw (g, r.removeFromRight (16.0f).withSizeKeepingCentre (12.0f, 12.0f), Icons::Glyph::down, row < numRows - 1 ? quiet : off);
    r.removeFromRight (6.0f);
    Icons::draw (g, r.removeFromRight (16.0f).withSizeKeepingCentre (12.0f, 12.0f), Icons::Glyph::up, row > 0 ? quiet : off);
    r.removeFromRight (6.0f);
}

// Que boton hay en x (dentro de una fila de ancho rowWidth con 8 px de margen a cada lado).
inline Hit hitTest (int x, int rowWidth)
{
    const int right = rowWidth - 8;
    if (x >= right - 19)
        return Hit::remove;
    if (x >= right - 41)
        return Hit::down;
    if (x >= right - 63)
        return Hit::up;
    return Hit::none;
}

// Lo que lleva el arrastre de una fila: prefijo e indice (p. ej. "automix-row:3").
inline juce::var describe (const juce::String& prefix, const juce::SparseSet<int>& rows)
{
    return rows.isEmpty() ? juce::var() : juce::var (prefix + juce::String (rows[0]));
}

inline int draggedRow (const juce::String& prefix, const juce::var& description)
{
    const auto text = description.toString();
    return text.startsWith (prefix) ? text.substring (prefix.length()).getIntValue() : -1;
}

// Donde caeria lo arrastrado: hueco entre filas (0 = antes de la primera, n = al final).
// 'position' en coordenadas de 'list'. Cerca de los bordes la lista se desplaza sola.
inline int dropIndex (juce::ListBox& list, juce::Point<int> position)
{
    if (auto* viewport = list.getViewport())
        viewport->autoScroll (position.x, position.y, 24, 12);
    return list.getInsertionIndexForPosition (position.x, position.y);
}

// La fila 'from' soltada en el hueco 'gap': la posicion final (o -1 si se queda donde esta).
inline int targetFor (int from, int gap)
{
    const int to = gap > from ? gap - 1 : gap;
    return to == from ? -1 : to;
}

// Raya del hueco donde caeria (coordenadas de 'parent', que contiene a 'list').
inline void drawDropLine (juce::Graphics& g, juce::Component& parent, juce::ListBox& list, int gap)
{
    if (gap < 0)
        return;
    const auto row = list.getRowPosition (gap, true);
    const auto top = parent.getLocalPoint (&list, juce::Point<int> (0, row.getY()));
    const auto bounds = parent.getLocalArea (&list, list.getLocalBounds());
    const float y = (float) juce::jlimit (bounds.getY(), bounds.getBottom() - 2, top.y);
    g.setColour (DarkLookAndFeel::accentA);
    g.fillRoundedRectangle ((float) bounds.getX() + 4.0f, y - 1.5f, (float) bounds.getWidth() - 8.0f, 3.0f, 1.5f);
}
}
