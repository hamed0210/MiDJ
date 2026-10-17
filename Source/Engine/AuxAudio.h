#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <vector>

namespace midj
{
// Un sonido que no es de un deck y se suma al master: el audio de una fuente Multimedia del
// video o el de una capturadora (como los canales del mezclador de audio de OBS).
class AuxSource
{
public:
    virtual ~AuxSource() = default;
    // Hilo de audio (y al registrarse, si el audio ya estaba en marcha).
    virtual void prepare (double sampleRate, int blockSize) = 0;
    // Hilo de audio: suma n muestras a left/right (right puede ser nullptr si la salida es mono).
    virtual void addTo (float* left, float* right, int n) = 0;
};

// Los sonidos auxiliares que suenan por el master. El hilo de audio los recorre con el cerrojo
// puesto; registrar y quitar (raro) lo toma un momento desde otro hilo.
class AuxBus
{
public:
    void add (AuxSource* source);
    void remove (AuxSource* source);   // al volver, el hilo de audio ya no lo usa

    void prepare (double sampleRate, int blockSize);
    void addTo (float* left, float* right, int n);

private:
    juce::CriticalSection lock;
    std::vector<AuxSource*> sources;
    double rate = 0.0;
    int block = 512;
};

// Una entrada de audio de Windows (p. ej. el audio HDMI de una capturadora) que se suma al
// master. Va por su propio dispositivo WASAPI compartido, con otro reloj que la salida: se
// guarda en una cola de unos 40 ms y se remuestrea ajustando un poco la velocidad para que la
// cola no se vacie ni crezca.
class AudioInputSource : public AuxSource,
                         private juce::AudioIODeviceCallback
{
public:
    AudioInputSource() = default;
    ~AudioInputSource() override;

    // Nombres de las entradas de audio (como los da WASAPI).
    static juce::StringArray inputNames();

    // Abre la entrada (otra ya abierta se cierra). "" = ninguna. false si no se pudo.
    bool open (const juce::String& name, juce::String& error);
    void close();
    juce::String getName() const { return name; }
    bool isOpen() const { return device != nullptr; }

    void setGain (float g) { gain = g; }
    float getLevel() const { return level.load(); }   // pico reciente (0..1) para un vumetro

    void prepare (double sampleRate, int blockSize) override;
    void addTo (float* left, float* right, int n) override;

    // Para pruebas: la cola sin dispositivo.
    void pushForTest (const float* left, const float* right, int n, double inputRate);

private:
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override;
    void audioDeviceStopped() override {}
    void push (const float* left, const float* right, int n);

    std::unique_ptr<juce::AudioIODeviceType> type;
    std::unique_ptr<juce::AudioIODevice> device;
    juce::String name;

    static constexpr int capacity = 1 << 15;   // por canal (~0,7 s a 48 kHz)
    juce::AbstractFifo fifo { capacity };
    juce::AudioBuffer<float> ring { 2, capacity };
    std::atomic<double> inputRate { 0.0 };
    std::atomic<float> gain { 1.0f }, level { 0.0f };
    std::atomic<bool> flush { true };   // vaciar la cola (la vacia el hilo de salida, que es el que lee)

    // Hilo de audio de la salida
    double outputRate = 0.0;
    std::array<juce::LagrangeInterpolator, 2> interpolators;
    juce::AudioBuffer<float> gathered { 2, 0 }, converted { 2, 0 };
    bool buffering = true;   // esperando a tener la cola llena antes de sonar
};
}
