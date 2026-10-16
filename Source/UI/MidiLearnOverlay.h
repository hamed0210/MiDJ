#pragma once

#include "Control/MidiController.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// Un control de la pantalla que se puede asignar: el componente (y opcionalmente una zona
// dentro de el, en sus coordenadas) y la accion que hace.
struct LearnTarget
{
    juce::Component* component = nullptr;
    juce::String action;
    juce::Rectangle<int> area;   // vacio = todo el componente
};

// Capa del modo "aprender MIDI": se pone encima de la ventana, marca los controles asignables
// (verde = ya asignado, naranja = esperando un control de la controladora) y recoge los clics:
//  - clic izquierdo en un control: esperar el siguiente control MIDI para asignarlo;
//  - clic derecho: quitar su asignacion.
class MidiLearnOverlay : public juce::Component,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit MidiLearnOverlay (MidiController& midi);
    ~MidiLearnOverlay() override;

    void setTargets (std::vector<LearnTarget> newTargets) { targets = std::move (newTargets); repaint(); }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void visibilityChanged() override;

private:
    juce::Rectangle<int> boundsOf (const LearnTarget& t) const;
    const LearnTarget* targetAt (juce::Point<int> p) const;
    juce::Rectangle<int> headerArea() const { return getLocalBounds().removeFromBottom (54).reduced (8, 4); }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void timerCallback() override { repaint (headerArea()); }

    MidiController& midi;
    std::vector<LearnTarget> targets;
};
}
