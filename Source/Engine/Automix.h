#pragma once

#include "Engine/Deck.h"
#include "Engine/Mixer.h"

#include <functional>
#include <vector>

namespace midj
{
// Automix de dos decks (el modo "dual deck" de VirtualDJ): la cancion siguiente se carga en el
// deck libre, entra en el momento planificado y el crossfader pasa de un lado al otro.
//
// Plan de cada transicion (modo Smart de VirtualDJ):
//  - A tempo, si la actual tiene fin de parte ritmica, la siguiente tiene inicio y sus BPM difieren
//    menos de un 6 %: 16 pulsos que
//    terminan en el fin de la parte ritmica de la actual, la siguiente entra en el inicio de la
//    suya con los compases alineados, y el tempo de las dos se desliza del de la actual al de la
//    siguiente mientras dura (con correccion de fase continua, como VirtualDJ).
//  - Si no: fundido de N segundos que termina en el fadeEnd de la actual; la siguiente entra en
//    su fadeStart.
// Todo ocurre en el hilo de interfaz: update() se llama a menudo (temporizador de la ventana).
class Automix : public juce::ChangeBroadcaster
{
public:
    struct Item
    {
        juce::File file;
        juce::String title, artist;
        double bpm = 0.0;
    };
    enum class State { off, waiting, playing, mixing };

    Automix (Deck& deckA, Deck& deckB, Mixer& mixer);

    // Como cargar un archivo en un deck (la ventana usa la biblioteca; las pruebas, memoria).
    // Devuelve false si no se puede abrir: esa cancion se salta.
    std::function<bool (int deckIndex, const juce::File& file)> loadTrack;

    // Lista.
    void add (const Item& item);
    void insertNext (const Item& item);
    void remove (int index);
    void clearQueue();
    const std::vector<Item>& getQueue() const { return queue; }
    bool saveQueue (const juce::File& file) const;
    void loadQueue (const juce::File& file);

    // Control.
    void setEnabled (bool shouldBeOn);
    bool isEnabled() const { return state != State::off; }
    void mixNow();   // empezar la transicion ya
    void skip();     // descartar la siguiente (si aun no ha entrado) y preparar la otra
    void setFadeSeconds (double s)  { fadeSeconds = juce::jlimit (0.5, 30.0, s); }
    double getFadeSeconds() const   { return fadeSeconds; }
    static constexpr int tempoMixBeats = 16;

    // Estado (para la interfaz).
    State getState() const { return state; }
    int getCurrentDeck() const { return current; }
    double getProgress() const { return progress; }   // 0-1 durante la transicion
    bool nextIsTempoMix() const { return planned && plan.tempoMix; }
    double getPlannedStart() const { return planned ? plan.start : -1.0; }
    juce::String getStatusText() const;
    const Item* getNext() const { return nextRequested ? &next : nullptr; }

    void update();

private:
    struct Plan
    {
        bool tempoMix = false;
        double start = 0.0, length = 0.0, entry = 0.0;   // en segundos de la cancion actual / siguiente
    };

    Deck& deck (int index) { return index == 0 ? deckA : deckB; }
    void requestNext();
    void makePlan();
    void startTransition();
    void updateTransition();
    void finishTransition();
    void setCrossfaderTo (int deckIndex, double amount);   // amount 0 = todo en 'current', 1 = todo en el otro

    Deck& deckA;
    Deck& deckB;
    Mixer& mixer;
    std::vector<Item> queue;
    double fadeSeconds = 6.0;

    State state = State::off;
    int current = 0;
    Item next;
    bool nextRequested = false, planned = false, mixNowRequested = false, startPending = false;
    Plan plan;
    double progress = 0.0, mixStartPosition = 0.0, startBpm = 0.0, targetBpm = 0.0;
};
}
