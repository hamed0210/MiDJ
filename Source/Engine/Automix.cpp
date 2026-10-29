#include "Engine/Automix.h"

#include <cmath>

namespace midj
{
namespace
{
    // Desfase entre dos fases de pulso (0-1), llevado a -0,5..0,5.
    double wrapHalf (double x)
    {
        x -= std::floor (x);
        return x > 0.5 ? x - 1.0 : x;
    }

    juce::String formatTime (double seconds)
    {
        const int s = (int) std::max (0.0, std::round (seconds));
        return juce::String::formatted ("%d:%02d", s / 60, s % 60);
    }

    // La cancion acabo sola (no se pauso a mano): parada y al final.
    bool reachedEnd (const Deck& d)
    {
        return ! d.isPlaying() && d.hasTrack() && d.getPositionSeconds() >= d.getLengthSeconds() - 0.5;
    }
}

Automix::Automix (Deck& a, Deck& b, Mixer& m) : deckA (a), deckB (b), mixer (m) {}

void Automix::add (const Item& item)
{
    queue.push_back (item);
    sendChangeMessage();
}

void Automix::insertNext (const Item& item)
{
    queue.insert (queue.begin(), item);
    sendChangeMessage();
}

void Automix::remove (int index)
{
    if (index >= 0 && index < (int) queue.size())
        queue.erase (queue.begin() + index);
    sendChangeMessage();
}

void Automix::move (int from, int to)
{
    const int n = (int) queue.size();
    if (! juce::isPositiveAndBelow (from, n) || ! juce::isPositiveAndBelow (to, n) || from == to)
        return;
    const auto item = queue[(size_t) from];
    queue.erase (queue.begin() + from);
    queue.insert (queue.begin() + to, item);
    sendChangeMessage();
}

void Automix::clearQueue()
{
    queue.clear();
    sendChangeMessage();
}

bool Automix::saveQueue (const juce::File& file) const
{
    juce::XmlElement xml ("AUTOMIX");
    for (const auto& item : queue)
    {
        auto* e = xml.createNewChildElement ("ITEM");
        e->setAttribute ("path", item.file.getFullPathName());
        e->setAttribute ("title", item.title);
        e->setAttribute ("artist", item.artist);
        e->setAttribute ("bpm", item.bpm);
    }
    file.getParentDirectory().createDirectory();
    return xml.writeTo (file);
}

void Automix::loadQueue (const juce::File& file)
{
    queue.clear();
    if (const auto xml = juce::XmlDocument::parse (file); xml != nullptr && xml->hasTagName ("AUTOMIX"))
        for (auto* e : xml->getChildWithTagNameIterator ("ITEM"))
        {
            Item item { juce::File (e->getStringAttribute ("path")), e->getStringAttribute ("title"),
                        e->getStringAttribute ("artist"), e->getDoubleAttribute ("bpm") };
            if (item.file.existsAsFile())
                queue.push_back (item);
        }
    sendChangeMessage();
}

void Automix::setEnabled (bool shouldBeOn)
{
    if (shouldBeOn == isEnabled())
        return;
    if (! shouldBeOn)
    {
        state = State::off;
        nextRequested = planned = mixNowRequested = startPending = false;
        progress = 0.0;
        sendChangeMessage();
        return;
    }

    // Deck de partida: el que suena (si suenan los dos, el que tiene el crossfader a su lado);
    // si no suena ninguno, el que tenga cancion; si ninguno tiene, la primera de la lista en A.
    const bool aPlays = deckA.isPlaying(), bPlays = deckB.isPlaying();
    if (aPlays && bPlays)        current = mixer.getCrossfader() <= 0.5f ? 0 : 1;
    else if (aPlays || bPlays)   current = aPlays ? 0 : 1;
    else if (deckA.hasTrack())   current = 0;
    else if (deckB.hasTrack())   current = 1;
    else if (! queue.empty() && loadTrack)
    {
        current = 0;
        const auto first = queue.front();
        queue.erase (queue.begin());
        if (! loadTrack (0, first.file))
            return;
    }
    else
        return;   // nada que poner

    // Si no suena nada, se arranca en cuanto el deck tenga la cancion.
    startPending = ! deck (current).isPlaying();
    // El otro deck se para (si sonaba) y el crossfader va al lado del que suena.
    if (deck (1 - current).isPlaying())
        deck (1 - current).pause();
    setCrossfaderTo (current, 0.0);
    nextRequested = planned = mixNowRequested = false;
    state = startPending ? State::waiting : State::playing;
    sendChangeMessage();
}

void Automix::setCrossfaderTo (int deckIndex, double amount)
{
    // 0 = todo en deckIndex; 1 = todo en el otro deck.
    const double x = deckIndex == 0 ? amount : 1.0 - amount;
    mixer.setCrossfader ((float) juce::jlimit (0.0, 1.0, x));
}

void Automix::mixNow()
{
    if (isEnabled() && state != State::mixing)
        mixNowRequested = true;
}

void Automix::skip()
{
    if (! isEnabled() || state == State::mixing || ! nextRequested)
        return;
    nextRequested = planned = false;   // la siguiente se descarta; update() carga la otra
    sendChangeMessage();
}

void Automix::requestNext()
{
    if (queue.empty() || ! loadTrack)
        return;
    next = queue.front();
    queue.erase (queue.begin());
    planned = false;
    nextRequested = loadTrack (1 - current, next.file);   // si falla, la proxima vuelta prueba la siguiente
    sendChangeMessage();
}

void Automix::makePlan()
{
    auto& cur = deck (current);
    auto& nxt = deck (1 - current);
    const auto& a = cur.getMixPoints();
    const auto& b = nxt.getMixPoints();
    const double bpmA = cur.getOriginalBpm(), bpmB = nxt.getOriginalBpm();
    const double now = cur.getPositionSeconds();

    // A tempo si la actual tiene fin de parte ritmica, la siguiente tiene inicio, y el cociente
    // de BPM esta entre 0,94 y 1,06 (la regla de VirtualDJ).
    const double ratio = bpmA > 0.0 ? bpmB / bpmA : 0.0;
    plan.tempoMix = bpmA > 0.0 && bpmB > 0.0 && a.hasTempoEnd() && b.hasTempoStart() && ratio > 0.94 && ratio < 1.06;

    if (plan.tempoMix)
    {
        const double period = 60.0 / bpmA;
        plan.length = tempoMixBeats * period;
        plan.start = a.tempoEnd - plan.length;
        if (plan.start < now)   // tarde: en el proximo compas
            plan.start = cur.getFirstBeat() + std::ceil ((now - cur.getFirstBeat()) / (4.0 * period)) * 4.0 * period;
        plan.entry = b.tempoStart;
    }
    else
    {
        plan.length = fadeSeconds;
        const double end = a.fadeEnd > 0.0 ? a.fadeEnd : cur.getLengthSeconds();
        plan.start = juce::jmax (now, end - plan.length);
        plan.entry = b.fadeStart > 0.0 ? b.fadeStart : b.realStart;
    }
    planned = true;
    sendChangeMessage();
}

void Automix::startTransition()
{
    auto& cur = deck (current);
    auto& nxt = deck (1 - current);
    const double bpmA = cur.getOriginalBpm(), bpmB = nxt.getOriginalBpm();

    if (plan.tempoMix)
    {
        // La siguiente arranca al tempo actual de la que suena, con los compases alineados: se
        // coloca tantos pulsos despues de su inicio como pulsos lleva el compas de la actual.
        startBpm = cur.getCurrentBpm();
        targetBpm = bpmB;
        nxt.setSpeed (startBpm / bpmB);
        double barBeats = std::fmod ((cur.getPositionSeconds() - cur.getFirstBeat()) * bpmA / 60.0, 4.0);
        if (barBeats < 0.0)
            barBeats += 4.0;
        if (barBeats > 2.0)
            barBeats -= 4.0;   // mas cerca del compas siguiente: entra un poco antes
        nxt.setPositionSeconds (plan.entry + barBeats * 60.0 / bpmB);
    }
    else
    {
        nxt.setSpeed (1.0);
        nxt.setPositionSeconds (plan.entry);
    }
    nxt.play();
    setCrossfaderTo (current, 0.0);
    mixStartPosition = cur.getPositionSeconds();
    progress = 0.0;
    mixNowRequested = false;
    state = State::mixing;
    sendChangeMessage();
}

void Automix::updateTransition()
{
    auto& cur = deck (current);
    auto& nxt = deck (1 - current);
    const double elapsed = cur.getPositionSeconds() - mixStartPosition;
    progress = cur.isPlaying() ? juce::jlimit (0.0, 1.0, elapsed / plan.length) : 1.0;
    setCrossfaderTo (current, progress);

    if (plan.tempoMix)
    {
        // Tempo comun que se desliza del de la actual al de la siguiente.
        const double common = startBpm + (targetBpm - startBpm) * progress;
        cur.setSpeed (common / cur.getOriginalBpm());
        // Correccion de fase: si la siguiente va por detras (desfase positivo), un poco mas rapida.
        const double error = wrapHalf (cur.getBeatPhase() - nxt.getBeatPhase());
        const double trim = juce::jlimit (-0.004, 0.004, error * 0.05);
        nxt.setSpeed (common / nxt.getOriginalBpm() * (1.0 + trim));
    }
    if (progress >= 1.0)
        finishTransition();
}

void Automix::finishTransition()
{
    auto& cur = deck (current);
    auto& nxt = deck (1 - current);
    cur.pause();
    cur.setSpeed (1.0);
    if (plan.tempoMix)
        nxt.setSpeed (1.0);   // la nueva queda a su propio tempo (el deslizamiento ya llego ahi)
    current = 1 - current;
    setCrossfaderTo (current, 0.0);
    nextRequested = planned = false;
    progress = 0.0;
    state = State::playing;
    sendChangeMessage();
}

void Automix::update()
{
    if (state == State::off)
        return;
    auto& cur = deck (current);
    auto& nxt = deck (1 - current);

    if (state == State::waiting)
    {
        if (cur.isLoading() || ! cur.hasTrack())
            return;
        cur.play();
        startPending = false;
        state = State::playing;
        sendChangeMessage();
    }

    if (state == State::mixing)
    {
        updateTransition();
        return;
    }

    // Se dio Play a mano al otro deck: manda la mano. Mientras suenan los dos no se toca nada (el
    // crossfader automatico se desliza y pausa el anterior); cuando el anterior se para, el
    // automix sigue desde el deck nuevo y prepara la siguiente en el que quedo libre.
    if (nxt.isPlaying())
    {
        if (cur.isPlaying())
            return;
        // Sono otra cancion (se cargo otro archivo a mano): la preparada vuelve a la lista.
        const auto loaded = nxt.getLoadedFile();
        if (nextRequested && loaded != juce::File() && loaded != next.file)
            queue.insert (queue.begin(), next);
        current = 1 - current;
        nextRequested = planned = mixNowRequested = false;
        sendChangeMessage();
        return;
    }

    // Preparar la siguiente en el deck libre.
    if (! nextRequested)
        requestNext();
    const bool nextReady = nextRequested && ! nxt.isLoading() && nxt.hasTrack();
    if (nextReady && ! planned)
        makePlan();

    if (nextReady && planned && (mixNowRequested || (cur.isPlaying() && cur.getPositionSeconds() >= plan.start) || reachedEnd (cur)))
    {
        if (mixNowRequested)
        {
            plan.start = cur.getPositionSeconds();
            if (plan.tempoMix)
                plan.length = tempoMixBeats * 60.0 / juce::jmax (1.0, cur.getOriginalBpm());
        }
        startTransition();
        return;
    }

    // Lista terminada: cuando la ultima cancion acaba, el automix se apaga. Pausar a mano no
    // cuenta como acabar: ni pasa a la siguiente ni se apaga (vuelve al dar Play).
    if (! nextRequested && reachedEnd (cur))
        setEnabled (false);
}

juce::String Automix::getStatusText() const
{
    switch (state)
    {
        case State::off:     return queue.empty() ? "Automix apagado. Agrega canciones a la lista." : "Automix apagado";
        case State::waiting: return "Cargando...";
        case State::mixing:  return juce::String ("Mezclando ") + (plan.tempoMix ? "a tempo " : "con fundido ")
                                    + juce::String (juce::roundToInt (progress * 100.0)) + " %";
        case State::playing: break;
    }
    if (! nextRequested)
        return "Ultima cancion de la lista";
    if (! planned)
        return "Preparando: " + next.title;
    const auto& cur = current == 0 ? deckA : deckB;
    return "Siguiente: " + next.title + " en " + formatTime ((plan.start - cur.getPositionSeconds()) / juce::jmax (0.01, cur.getSpeed()))
           + (plan.tempoMix ? " (a tempo, 16 pulsos)" : " (fundido de " + juce::String (fadeSeconds, 0) + " s)");
}
}
