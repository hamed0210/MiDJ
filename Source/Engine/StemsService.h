#pragma once

#include "Engine/StemSeparator.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <deque>
#include <map>

namespace midj
{
// Prepara los stems en segundo plano, de una cancion en una, con prioridad baja:
//  - si la voz de esa cancion ya esta en la cache (%APPDATA%\MiDJ\Stems\*.flac) solo la lee;
//  - si no, separa la cancion entera y guarda la voz en la cache (FLAC de 24 bits).
// Avisa en el hilo de interfaz con onReady. Los pedidos urgentes (un deck) van primero.
//
// Dispositivo: CPU por defecto. La GPU (DirectML) se puede activar en stems.xml; si falla,
// se vuelve a la CPU y se apunta para no volver a intentarlo (en algunas graficas la GPU se
// cuelga y Windows la reinicia: no se prueba sola).
class StemsService : private juce::Thread
{
public:
    enum class Status { none, queued, working, ready, failed };

    StemsService (juce::AudioFormatManager& formats, const juce::File& dataFolder, const juce::File& modelFolder);
    ~StemsService() override;

    void request (const juce::File& song, bool urgent);
    Status getStatus (const juce::File& song) const;
    double getProgress (const juce::File& song) const;   // 0-1 mientras se separa
    bool isCached (const juce::File& song) const;
    juce::File cacheFileFor (const juce::File& song) const;
    juce::String getDeviceDescription() const;

    // Hilo de interfaz: la voz de 'song' (mismo tamano y frecuencia que la cancion decodificada).
    std::function<void (const juce::File& song, std::shared_ptr<const juce::AudioBuffer<float>> vocals)> onReady;
    std::function<void (const juce::File& song, const juce::String& error)> onFailed;
    std::function<void()> onProgress;   // hilo de interfaz, para refrescar los porcentajes

private:
    void run() override;
    bool ensureModel (juce::String& error);
    std::shared_ptr<juce::AudioBuffer<float>> separate (const juce::File& song, juce::String& error);
    void setStatus (const juce::File& song, Status s, double progress = 0.0);

    juce::AudioFormatManager& formats;
    const juce::File cacheFolder, settingsFile, modelFolder;
    StemSeparator separator;
    bool modelTried = false;

    mutable juce::CriticalSection lock;
    std::deque<juce::File> queue;
    std::map<juce::String, std::pair<Status, double>> states;   // por ruta
    juce::WaitableEvent wake;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
}
