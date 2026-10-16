#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace midj
{
// Acciones que se pueden asignar a un control MIDI, como los verbos de VDJScript de VirtualDJ
// pero sin lenguaje: cada control hace una sola cosa.
enum class ActionType
{
    button,     // pulsar (nota o CC > 0): play, cue, sync...
    absolute,   // posicion 0-1: faders y potenciometros
    relative    // pasos +-: jog y encoders sin fin
};

struct ActionInfo
{
    juce::String id;     // "deck1.play", "crossfader"...
    juce::String name;   // para la interfaz: "Play A"
    ActionType type;
};

namespace Actions
{
    const std::vector<ActionInfo>& all();
    const ActionInfo* find (const juce::String& id);
    // "deck1.play" -> 0; "deck2.cue" -> 1; acciones globales -> -1.
    int deckIndex (const juce::String& id);
    // "deck1.play" -> "play".
    juce::String verb (const juce::String& id);
}
}
