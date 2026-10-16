#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <optional>
#include <vector>

namespace midj
{
// Una asignacion: "este mensaje MIDI hace esta accion".
struct MidiBinding
{
    enum class Kind { note, cc, pitchBend };
    // Como manda sus pasos un encoder sin fin: complemento a dos (1 = +1, 127 = -1) o
    // desplazado (65 = +1, 63 = -1). Los faders y botones son 'absolute'.
    enum class Encoding { absolute, relativeTwos, relativeOffset };

    int channel = 1;     // 1-16
    Kind kind = Kind::cc;
    int number = 0;      // nota o CC (no se usa en pitch bend)
    Encoding encoding = Encoding::absolute;
    juce::String action;

    bool sameControl (const MidiBinding& other) const
    {
        return channel == other.channel && kind == other.kind && (kind == Kind::pitchBend || number == other.number);
    }
    juce::String describe() const;   // "CC 7 (canal 1)"

    // El control que manda un mensaje, si es de los que se pueden asignar.
    static std::optional<MidiBinding> controlOf (const juce::MidiMessage& m);

    // Conversiones de valores.
    static double unitFromCC (int value);         // 0-127 -> 0-1 con 64 = 0,5 exacto (centro de los EQ)
    static double unitFromPitchBend (int value);  // 0-16383 -> 0-1 con 8192 = 0,5
    static int ticksFrom (int value, Encoding e); // encoder: pasos con signo
};

// Todas las asignaciones, guardables en XML.
class MidiMapping
{
public:
    // Asigna; un control solo hace una accion (si ya tenia otra, se sustituye).
    void bind (const MidiBinding& binding);
    void removeAction (const juce::String& action);
    const MidiBinding* find (const MidiBinding& control) const;
    std::vector<MidiBinding> bindingsFor (const juce::String& action) const;
    const std::vector<MidiBinding>& getAll() const { return bindings; }
    void clear() { bindings.clear(); }

    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement& xml);
    bool save (const juce::File& file) const;
    bool load (const juce::File& file);

private:
    std::vector<MidiBinding> bindings;
};
}
