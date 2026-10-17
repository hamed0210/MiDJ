#pragma once

#include "Control/Actions.h"
#include "Engine/Deck.h"
#include "Engine/Mixer.h"

#include <functional>

namespace midj
{
// Ejecuta las acciones sobre el motor (decks, mezclador, efectos). Lo que es seguro desde
// cualquier hilo (atomicos del motor) se hace al momento, en el hilo MIDI, para que la latencia
// sea minima; lo que toca la interfaz (cargar, mover el navegador) se pasa al hilo de interfaz.
class ControlSurface
{
public:
    ControlSurface (Deck& deckA, Deck& deckB, Mixer& mixer);
    ~ControlSurface() { *alive = false; }

    // Ganchos hacia la interfaz (se llaman siempre en el hilo de interfaz).
    std::function<void (int deckIndex)> onLoadSelected;    // cargar en el deck lo elegido en el navegador
    std::function<void (int steps)> onBrowserScroll;       // mover la seleccion del navegador

    void pressButton (const juce::String& action);              // acciones de tipo boton
    void releaseButton (const juce::String& action);            // soltar (pads del sampler en modo hold)
    void setAbsolute (const juce::String& action, double unit); // 0-1
    void moveRelative (const juce::String& action, int ticks);  // pasos con signo

    static constexpr double tempoRange = 0.16;   // +-16 %, igual que el fader de la interfaz

private:
    Deck& deckFor (int index) { return index == 0 ? deckA : deckB; }

    Deck& deckA;
    Deck& deckB;
    Mixer& mixer;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);   // para los callAsync
};
}
