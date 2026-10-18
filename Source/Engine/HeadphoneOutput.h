#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <array>

namespace midj
{
// Audifonos por otra tarjeta de sonido (p. ej. un adaptador USB): el hilo de audio de la salida
// principal deja aqui lo de los audifonos y el dispositivo de los audifonos, con su propio reloj,
// lo recoge de una cola de unos 40 ms remuestreando un poco mas deprisa o mas despacio para que
// la cola no se vacie ni crezca (como la entrada de audio de una capturadora, al reves).
class HeadphoneOutput : private juce::AudioIODeviceCallback
{
public:
    HeadphoneOutput() = default;
    ~HeadphoneOutput() override;

    // Salidas de audio (como las da WASAPI).
    static juce::StringArray outputNames();

    // Abre la salida (otra ya abierta se cierra). "" = ninguna. false si no se pudo.
    bool open (const juce::String& name, juce::String& error);
    void close();
    juce::String getName() const { return name; }
    bool isOpen() const { return device != nullptr; }

    // Hilo de audio principal.
    void prepare (double sampleRate);
    void push (const float* left, const float* right, int n);

    // Para pruebas: sacar n muestras como lo haria el dispositivo a outputRate.
    void pullForTest (float* left, float* right, int n, double outputRate);

private:
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override;
    void audioDeviceStopped() override {}
    void pull (float* left, float* right, int n, double outRate);

    std::unique_ptr<juce::AudioIODeviceType> type;
    std::unique_ptr<juce::AudioIODevice> device;
    juce::String name;

    static constexpr int capacity = 1 << 15;   // por canal (~0,7 s a 48 kHz)
    juce::AbstractFifo fifo { capacity };
    juce::AudioBuffer<float> ring { 2, capacity };
    std::atomic<double> sourceRate { 0.0 };

    // Hilo del dispositivo de los audifonos.
    std::array<juce::LagrangeInterpolator, 2> interpolators;
    juce::AudioBuffer<float> gathered { 2, 8192 };
    bool buffering = true;   // esperando a tener la cola llena antes de sonar
};
}
