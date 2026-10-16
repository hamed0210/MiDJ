#include "Control/MidiMapping.h"

namespace midj
{
namespace
{
    const char* kindName (MidiBinding::Kind k)
    {
        switch (k)
        {
            case MidiBinding::Kind::note:      return "note";
            case MidiBinding::Kind::pitchBend: return "pitchbend";
            default:                           return "cc";
        }
    }

    const char* encodingName (MidiBinding::Encoding e)
    {
        switch (e)
        {
            case MidiBinding::Encoding::relativeTwos:   return "relative-twos";
            case MidiBinding::Encoding::relativeOffset: return "relative-offset";
            default:                                    return "absolute";
        }
    }
}

juce::String MidiBinding::describe() const
{
    juce::String s;
    switch (kind)
    {
        case Kind::note:      s = "Nota " + juce::String (number); break;
        case Kind::pitchBend: s = "Pitch bend"; break;
        default:              s = "CC " + juce::String (number); break;
    }
    return s + " (canal " + juce::String (channel) + ")";
}

std::optional<MidiBinding> MidiBinding::controlOf (const juce::MidiMessage& m)
{
    MidiBinding b;
    b.channel = m.getChannel();
    if (m.isNoteOnOrOff())
    {
        b.kind = Kind::note;
        b.number = m.getNoteNumber();
    }
    else if (m.isController())
    {
        b.kind = Kind::cc;
        b.number = m.getControllerNumber();
    }
    else if (m.isPitchWheel())
    {
        b.kind = Kind::pitchBend;
        b.number = 0;
    }
    else
    {
        return std::nullopt;   // reloj, sysex, aftertouch...: no se asignan
    }
    return b;
}

double MidiBinding::unitFromCC (int value)
{
    value = juce::jlimit (0, 127, value);
    return value <= 64 ? value / 128.0 : 0.5 + (value - 64) / 126.0;
}

double MidiBinding::unitFromPitchBend (int value)
{
    value = juce::jlimit (0, 16383, value);
    return value <= 8192 ? value / 16384.0 : 0.5 + (value - 8192) / 16382.0;
}

int MidiBinding::ticksFrom (int value, Encoding e)
{
    if (e == Encoding::relativeOffset)
        return value - 64;
    return value < 64 ? value : value - 128;   // complemento a dos de 7 bits
}

void MidiMapping::bind (const MidiBinding& binding)
{
    bindings.erase (std::remove_if (bindings.begin(), bindings.end(),
                                    [&] (const MidiBinding& b) { return b.sameControl (binding); }),
                    bindings.end());
    bindings.push_back (binding);
}

void MidiMapping::removeAction (const juce::String& action)
{
    bindings.erase (std::remove_if (bindings.begin(), bindings.end(),
                                    [&] (const MidiBinding& b) { return b.action == action; }),
                    bindings.end());
}

const MidiBinding* MidiMapping::find (const MidiBinding& control) const
{
    for (const auto& b : bindings)
        if (b.sameControl (control))
            return &b;
    return nullptr;
}

std::vector<MidiBinding> MidiMapping::bindingsFor (const juce::String& action) const
{
    std::vector<MidiBinding> result;
    for (const auto& b : bindings)
        if (b.action == action)
            result.push_back (b);
    return result;
}

std::unique_ptr<juce::XmlElement> MidiMapping::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("MIDIMAPPING");
    xml->setAttribute ("version", 1);
    for (const auto& b : bindings)
    {
        auto* e = xml->createNewChildElement ("BIND");
        e->setAttribute ("action", b.action);
        e->setAttribute ("channel", b.channel);
        e->setAttribute ("kind", kindName (b.kind));
        e->setAttribute ("number", b.number);
        e->setAttribute ("encoding", encodingName (b.encoding));
    }
    return xml;
}

void MidiMapping::fromXml (const juce::XmlElement& xml)
{
    bindings.clear();
    for (auto* e : xml.getChildWithTagNameIterator ("BIND"))
    {
        MidiBinding b;
        b.action = e->getStringAttribute ("action");
        b.channel = juce::jlimit (1, 16, e->getIntAttribute ("channel", 1));
        const auto kind = e->getStringAttribute ("kind");
        b.kind = kind == "note" ? MidiBinding::Kind::note : kind == "pitchbend" ? MidiBinding::Kind::pitchBend : MidiBinding::Kind::cc;
        b.number = juce::jlimit (0, 127, e->getIntAttribute ("number"));
        const auto enc = e->getStringAttribute ("encoding");
        b.encoding = enc == "relative-twos" ? MidiBinding::Encoding::relativeTwos
                   : enc == "relative-offset" ? MidiBinding::Encoding::relativeOffset : MidiBinding::Encoding::absolute;
        if (b.action.isNotEmpty())
            bind (b);
    }
}

bool MidiMapping::save (const juce::File& file) const
{
    file.getParentDirectory().createDirectory();
    return toXml()->writeTo (file);
}

bool MidiMapping::load (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file); xml != nullptr && xml->hasTagName ("MIDIMAPPING"))
    {
        fromXml (*xml);
        return true;
    }
    return false;
}
}
