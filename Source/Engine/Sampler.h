#pragma once

#include "Engine/Effects.h"
#include "Engine/SampleData.h"

#include <array>
#include <atomic>
#include <functional>

namespace midj
{
// Sampler de 8 pads, como el de VirtualDJ:
//  - Disparo: on/off (pulsar arranca / para), hold (suena mientras se mantiene) o stutter
//    (cada pulsacion vuelve a empezar).
//  - Tempo: libre (a su velocidad), sincronizado (velocidad = BPM del deck / BPM del sonido,
//    doblada o dividida hasta quedar entre 0,66 y 1,5) o bloqueado (ademas, alineado al pulso).
//  - Fundidos de 128 muestras (a 44,1 kHz) al arrancar y al parar: sin clics.
//  - Grabar N pulsos de un deck en un pad: empieza justo en el siguiente pulso y queda como
//    bucle bloqueado al tempo.
// La interfaz y el MIDI llaman a press/release/ajustes (atomicos); el hilo de audio mezcla.
class Sampler : private juce::AsyncUpdater
{
public:
    static constexpr int numPads = 8;
    enum class Trigger { onOff = 0, hold, stutter };
    enum class TempoMode { free = 0, sync, locked };

    // Referencia de tempo para los pads (la da el mezclador: el deck que suena).
    struct Reference
    {
        bool valid = false;
        double bpm = 0.0;
        double beatAtStart = 0.0;     // pulso del deck en la primera muestra del bloque
        double samplesPerBeat = 0.0;  // a la frecuencia de salida
    };

    Sampler();
    ~Sampler() override;

    void prepare (double sampleRate, int maxBlockSize);

    // Sonidos (hilo de interfaz). Al poner un sonido se toman sus ajustes de bucle y tempo.
    void setSample (int pad, std::shared_ptr<SampleData> sample);
    std::shared_ptr<const SampleData> getSample (int pad) const;
    void clearPad (int pad) { setSample (pad, nullptr); }

    void setTrigger (int pad, Trigger t)      { pads[(size_t) pad].trigger = (int) t; }
    Trigger getTrigger (int pad) const        { return (Trigger) pads[(size_t) pad].trigger.load(); }
    void setLoop (int pad, bool loop)         { pads[(size_t) pad].loop = loop; }
    bool getLoop (int pad) const              { return pads[(size_t) pad].loop.load(); }
    void setTempoMode (int pad, TempoMode m)  { pads[(size_t) pad].tempoMode = (int) m; }
    TempoMode getTempoMode (int pad) const    { return (TempoMode) pads[(size_t) pad].tempoMode.load(); }
    void setVolume (float v)                  { volume = juce::jlimit (0.0f, 1.0f, v); }
    float getVolume() const                   { return volume.load(); }

    // Banco guardado (hilo de interfaz): rutas de los sonidos y ajustes de cada pad. Las
    // grabaciones se guardan como WAV en 'recordingsFolder' la primera vez.
    bool saveBank (const juce::File& xmlFile, const juce::File& recordingsFolder);
    bool loadBank (const juce::File& xmlFile, juce::AudioFormatManager& formats);

    // Disparo (cualquier hilo).
    void press (int pad);
    void release (int pad);
    void stopAll();
    bool isPlaying (int pad) const    { return pads[(size_t) pad].playing.load(); }
    double getProgress (int pad) const { return pads[(size_t) pad].progress.load(); }   // 0-1

    // Grabacion de 'beats' pulsos del deck 'deckIndex' en 'pad'. Termina en el hilo de interfaz
    // llamando a onRecordingFinished.
    void startRecording (int pad, int deckIndex, int beats);
    void cancelRecording();
    int getRecordingPad() const;   // -1 si no se graba
    // Crea ya el sonido de una grabacion terminada (normalmente lo hace solo el hilo de interfaz).
    void finishPendingRecording() { handleUpdateNowIfNeeded(); }
    std::function<void (int pad)> onRecordingFinished;
    static constexpr double maxRecordSeconds = 20.0;

    // Hilo de audio. capture: lo que sale de cada deck (para grabar). process: suma los pads.
    void capture (int deckIndex, const float* left, const float* right, int n, const TempoInfo& tempo);
    void process (float* left, float* right, int n, const Reference& reference);

    // Velocidad de un pad sincronizado: BPM del deck / BPM del sonido, plegado a 0,66-1,5.
    // 'beatFactor' devuelve cuantos pulsos del sonido avanzan por cada pulso del deck.
    static double syncSpeed (double deckBpm, double sampleBpm, double* beatFactor = nullptr);

private:
    struct Pad
    {
        std::shared_ptr<SampleData> sample;   // se cambia con el cerrojo
        std::atomic<int> trigger { 0 }, tempoMode { 0 };
        std::atomic<bool> loop { false };
        std::atomic<bool> wanted { false };     // lo que pide el usuario
        std::atomic<int> restarts { 0 };        // contador de pulsaciones en modo stutter
        std::atomic<bool> playing { false };    // lo que suena de verdad (para la interfaz)
        std::atomic<double> progress { 0.0 };
        // Solo del hilo de audio.
        bool active = false, restartPending = false;
        int seenRestarts = 0;
        double position = 0.0;
        float gain = 0.0f, target = 0.0f;
        const SampleData* current = nullptr;
    };

    double startPosition (const Pad& pad, const SampleData& s, const Reference& ref, double factor) const;
    void handleAsyncUpdate() override;

    std::array<Pad, numPads> pads;
    juce::SpinLock samplesLock;
    std::atomic<float> volume { 0.8f };
    double outputRate = 44100.0;
    float fadeStep = 1.0f / 128.0f;

    // Grabacion: 0 sin grabar, 1 esperando el pulso, 2 grabando, 3 terminada (falta crear el sonido).
    std::atomic<int> recordState { 0 };
    std::atomic<int> recordPad { -1 }, recordDeck { 0 }, recordBeats { 4 };
    int recordTarget = 0, recordWritten = 0;
    double recordBpm = 0.0;
    juce::AudioBuffer<float> recordBuffer;
};
}
