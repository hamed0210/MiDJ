#include "Control/ControlSurface.h"

namespace midj
{
ControlSurface::ControlSurface (Deck& a, Deck& b, Mixer& m) : deckA (a), deckB (b), mixer (m) {}

void ControlSurface::pressButton (const juce::String& action)
{
    if (action.startsWith ("sampler."))
    {
        auto& sampler = mixer.getSampler();
        if (action == "sampler.stop")
            sampler.stopAll();
        else if (const int pad = action.getTrailingIntValue() - 1; pad >= 0 && pad < Sampler::numPads)
            sampler.press (pad);
        return;
    }
    const int d = Actions::deckIndex (action);
    if (d < 0)
        return;
    auto& deck = deckFor (d);
    const auto verb = Actions::verb (action);

    if (verb == "play")
        deck.isPlaying() ? deck.pause() : deck.play();
    else if (verb == "cue")
        deck.cue();
    else if (verb == "sync")
        deck.syncTo (deckFor (1 - d));
    else if (verb == "keylock")
        deck.setKeyLock (! deck.isKeyLocked());
    else if (verb == "load")
        juce::MessageManager::callAsync ([this, d, flag = alive] { if (flag->load() && onLoadSelected) onLoadSelected (d); });
    else if (verb.startsWith ("loop"))
    {
        const int beats = verb.getTrailingIntValue();
        if (deck.getLoopBeats() == beats) deck.exitLoop();
        else                              deck.setLoop (beats);
    }
    else if (verb.startsWith ("fx"))
    {
        auto& rack = mixer.getEffects (d);
        const int slot = verb.getTrailingIntValue() - 1;
        if (slot >= 0 && slot < EffectRack::numSlots)
            rack.setOn (slot, ! rack.isOn (slot));
    }
}

void ControlSurface::releaseButton (const juce::String& action)
{
    if (action.startsWith ("sampler.pad"))
        if (const int pad = action.getTrailingIntValue() - 1; pad >= 0 && pad < Sampler::numPads)
            mixer.getSampler().release (pad);
}

void ControlSurface::setAbsolute (const juce::String& action, double unit)
{
    unit = juce::jlimit (0.0, 1.0, unit);
    if (action == "sampler.volume")
    {
        mixer.getSampler().setVolume ((float) unit);
        return;
    }
    if (action == "crossfader")
    {
        mixer.setCrossfader ((float) unit);
        return;
    }
    const int d = Actions::deckIndex (action);
    if (d < 0)
        return;
    const auto verb = Actions::verb (action);
    auto& strip = mixer.getStrip (d);
    const auto v = (float) unit;

    if      (verb == "tempo")  deckFor (d).setSpeed (1.0 + (unit * 2.0 - 1.0) * tempoRange);
    else if (verb == "gain")   strip.setGain (v);
    else if (verb == "high")   strip.setHigh (v);
    else if (verb == "mid")    strip.setMid (v);
    else if (verb == "low")    strip.setLow (v);
    else if (verb == "filter") strip.setFilter (v);
    else if (verb == "volume") strip.setVolume (v);
    else if (verb.startsWith ("fx") && verb.endsWith ("_amount"))
    {
        const int slot = verb.fromFirstOccurrenceOf ("fx", false, false).getIntValue() - 1;
        if (slot >= 0 && slot < EffectRack::numSlots)
            mixer.getEffects (d).setAmount (slot, v);
    }
}

void ControlSurface::moveRelative (const juce::String& action, int ticks)
{
    if (ticks == 0)
        return;
    if (action == "browser.scroll")
    {
        juce::MessageManager::callAsync ([this, ticks, flag = alive] { if (flag->load() && onBrowserScroll) onBrowserScroll (ticks); });
        return;
    }
    const int d = Actions::deckIndex (action);
    if (d >= 0 && Actions::verb (action) == "jog")
        deckFor (d).jog (ticks);
}
}
