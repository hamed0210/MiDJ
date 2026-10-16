#include "UI/MidiLearnOverlay.h"
#include "UI/DarkLookAndFeel.h"

namespace midj
{
namespace
{
    const juce::Colour mappedColour { 0xff4caf50 };    // verde
    const juce::Colour waitingColour { 0xffffa726 };   // naranja
}

MidiLearnOverlay::MidiLearnOverlay (MidiController& m) : midi (m)
{
    midi.addChangeListener (this);
    setInterceptsMouseClicks (true, false);
}

MidiLearnOverlay::~MidiLearnOverlay()
{
    midi.removeChangeListener (this);
}

void MidiLearnOverlay::visibilityChanged()
{
    if (isVisible())
        startTimerHz (10);   // el ultimo mensaje MIDI recibido se refresca en la cabecera
    else
    {
        stopTimer();
        midi.stopLearning();
    }
}

juce::Rectangle<int> MidiLearnOverlay::boundsOf (const LearnTarget& t) const
{
    if (t.component == nullptr || ! t.component->isShowing())
        return {};
    const auto local = t.area.isEmpty() ? t.component->getLocalBounds() : t.area;
    return getLocalArea (t.component, local);
}

const LearnTarget* MidiLearnOverlay::targetAt (juce::Point<int> p) const
{
    // El mas pequeno que contenga el punto (la zona del jog esta dentro del deck, etc.).
    const LearnTarget* best = nullptr;
    int bestArea = std::numeric_limits<int>::max();
    for (const auto& t : targets)
    {
        const auto r = boundsOf (t);
        if (r.contains (p) && r.getWidth() * r.getHeight() < bestArea)
        {
            best = &t;
            bestArea = r.getWidth() * r.getHeight();
        }
    }
    return best;
}

void MidiLearnOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.35f));
    const auto waiting = midi.getLearningAction();

    for (const auto& t : targets)
    {
        const auto r = boundsOf (t).toFloat().reduced (1.0f);
        if (r.isEmpty())
            continue;
        const bool isWaiting = t.action == waiting;
        const bool mapped = midi.describeBindings (t.action).isNotEmpty();
        if (isWaiting || mapped)
        {
            g.setColour ((isWaiting ? waitingColour : mappedColour).withAlpha (0.3f));
            g.fillRoundedRectangle (r, 4.0f);
        }
        g.setColour (isWaiting ? waitingColour : mapped ? mappedColour : DarkLookAndFeel::text.withAlpha (0.5f));
        g.drawRoundedRectangle (r, 4.0f, isWaiting ? 3.0f : 1.2f);
    }

    // Cabecera (abajo, sobre el navegador): instrucciones, aprendizaje en curso y controladoras.
    const auto header = headerArea();
    g.setColour (DarkLookAndFeel::panel.withAlpha (0.95f));
    g.fillRoundedRectangle (header.toFloat(), 6.0f);
    auto text = header.reduced (12, 4);
    g.setColour (DarkLookAndFeel::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    juce::String line1;
    if (waiting.isNotEmpty())
    {
        const auto* info = Actions::find (waiting);
        line1 = "Mueve o pulsa el control de tu controladora para: " + (info != nullptr ? info->name : waiting);
        g.setColour (waitingColour);
    }
    else
    {
        line1 = "APRENDER MIDI: clic en un control de la pantalla y despues muevelo en la controladora. "
                "Clic derecho: quitar. Boton MIDI: salir.";
    }
    g.drawText (line1, text.removeFromTop (22), juce::Justification::centredLeft, true);

    const auto devices = midi.getDeviceNames();
    const auto last = midi.getLastMessageText();
    g.setColour (DarkLookAndFeel::textQuiet);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Controladoras: " + (devices.isEmpty() ? juce::String ("ninguna conectada") : devices.joinIntoString (", "))
                    + (last.isNotEmpty() ? "      Ultimo mensaje: " + last : juce::String()),
                text, juce::Justification::centredLeft, true);
}

void MidiLearnOverlay::mouseDown (const juce::MouseEvent& e)
{
    const auto* t = targetAt (e.getPosition());
    if (t == nullptr)
    {
        midi.stopLearning();
        return;
    }
    if (e.mods.isPopupMenu())
        midi.removeAction (t->action);
    else
        midi.startLearning (t->action);
}
}
