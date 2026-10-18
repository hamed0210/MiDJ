#include "Control/Actions.h"

namespace midj::Actions
{
const std::vector<ActionInfo>& all()
{
    static const std::vector<ActionInfo> list = []
    {
        std::vector<ActionInfo> v;
        for (int d = 1; d <= 2; ++d)
        {
            const juce::String p = "deck" + juce::String (d) + ".", n = d == 1 ? " A" : " B";
            v.push_back ({ p + "play", "Play" + n, ActionType::button });
            v.push_back ({ p + "cue", "CUE" + n, ActionType::button });
            v.push_back ({ p + "sync", "SYNC" + n, ActionType::button });
            v.push_back ({ p + "keylock", "KEY" + n, ActionType::button });
            v.push_back ({ p + "vocals", "Voz" + n, ActionType::button });
            v.push_back ({ p + "instrumental", "Instrumental" + n, ActionType::button });
            v.push_back ({ p + "pfl", "Pre-escucha" + n, ActionType::button });
            v.push_back ({ p + "load", "Cargar" + n, ActionType::button });
            for (int beats : { 1, 2, 4, 8 })
                v.push_back ({ p + "loop" + juce::String (beats), "Loop " + juce::String (beats) + n, ActionType::button });
            v.push_back ({ p + "tempo", "Tempo" + n, ActionType::absolute });
            v.push_back ({ p + "jog", "Jog" + n, ActionType::relative });
            v.push_back ({ p + "gain", "Gain" + n, ActionType::absolute });
            v.push_back ({ p + "high", "Agudos" + n, ActionType::absolute });
            v.push_back ({ p + "mid", "Medios" + n, ActionType::absolute });
            v.push_back ({ p + "low", "Graves" + n, ActionType::absolute });
            v.push_back ({ p + "filter", "Filtro" + n, ActionType::absolute });
            v.push_back ({ p + "volume", "Volumen" + n, ActionType::absolute });
            for (int s = 1; s <= 3; ++s)
            {
                v.push_back ({ p + "fx" + juce::String (s), "FX" + juce::String (s) + n, ActionType::button });
                v.push_back ({ p + "fx" + juce::String (s) + "_amount", "Mando FX" + juce::String (s) + n, ActionType::absolute });
            }
        }
        v.push_back ({ "crossfader", "Crossfader", ActionType::absolute });
        for (int pad = 1; pad <= 8; ++pad)
            v.push_back ({ "sampler.pad" + juce::String (pad), "Sampler pad " + juce::String (pad), ActionType::button });
        v.push_back ({ "sampler.stop", "Sampler STOP", ActionType::button });
        v.push_back ({ "sampler.volume", "Sampler volumen", ActionType::absolute });
        v.push_back ({ "browser.scroll", "Navegador: mover", ActionType::relative });
        v.push_back ({ "automix.toggle", "Automix on/off", ActionType::button });
        v.push_back ({ "automix.mixnow", "Automix: mezclar ya", ActionType::button });
        v.push_back ({ "automix.skip", "Automix: saltar", ActionType::button });
        return v;
    }();
    return list;
}

const ActionInfo* find (const juce::String& id)
{
    for (const auto& a : all())
        if (a.id == id)
            return &a;
    return nullptr;
}

int deckIndex (const juce::String& id)
{
    if (id.startsWith ("deck1.")) return 0;
    if (id.startsWith ("deck2.")) return 1;
    return -1;
}

juce::String verb (const juce::String& id)
{
    return id.fromFirstOccurrenceOf (".", false, false);
}
}
