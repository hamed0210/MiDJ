#pragma once

#include "Control/ControlSurface.h"
#include "Control/MidiMapping.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace midj
{
// Controladoras MIDI: abre todas las entradas (y las que se conecten despues), traduce cada
// mensaje con el mapeo y ejecuta la accion. Tiene un modo "aprender": se elige una accion y
// el siguiente control que se mueva queda asignado a ella.
class MidiController : public juce::ChangeBroadcaster,
                       private juce::MidiInputCallback,
                       private juce::Timer
{
public:
    explicit MidiController (ControlSurface& surface);
    ~MidiController() override;

    // Mapeo guardado. Por defecto %APPDATA%\MiDJ\midi.xml.
    static juce::File getDefaultMappingFile();
    void loadMapping (const juce::File& file = getDefaultMappingFile());
    void setMappingFile (const juce::File& file) { mappingFile = file; }   // donde se guarda al cambiar

    // Abre las entradas MIDI y vigila si se conectan o desconectan (cada 2 s).
    void startDevices();
    juce::StringArray getDeviceNames() const;

    // Aprender. El aviso de "asignado" llega con sendChangeMessage (hilo de interfaz).
    void startLearning (const juce::String& actionId);
    void stopLearning();
    juce::String getLearningAction() const;
    void removeAction (const juce::String& actionId);
    juce::String describeBindings (const juce::String& actionId) const;   // "" si no tiene
    MidiMapping getMappingCopy() const;

    // Entrada de mensajes (cualquier hilo). Las controladoras llegan por aqui y tambien el
    // modo de simulacion y las pruebas.
    void handleMessage (const juce::MidiMessage& message);
    juce::String getLastMessageText() const;   // "CC 7 (canal 1) = 100", para la interfaz

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message) override { handleMessage (message); }
    void timerCallback() override { refreshDevices(); }
    void refreshDevices();
    void saveSoon();

    ControlSurface& surface;
    juce::File mappingFile = getDefaultMappingFile();
    mutable juce::CriticalSection lock;   // mapeo, accion en aprendizaje y ultimo mensaje
    MidiMapping mapping;
    juce::String learning, lastMessage;
    std::vector<std::unique_ptr<juce::MidiInput>> inputs;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
}
