#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>

namespace RubberBand { class RubberBandStretcher; }

namespace midj
{
// Cambio de tempo sin cambio de tono (keylock / master tempo), con Rubber Band en modo
// tiempo real. Aisla la libreria del resto del motor: cambiarla por otra solo toca este archivo.
//
// Funciona "tirando": el deck pide N muestras de salida y el estirador pide a su vez la
// entrada que necesita a una Source, que lee la cancion ya remuestreada a la frecuencia
// de salida (tempo original). La entrada va por delante de la salida: esa es su latencia.
class TimeStretcher
{
public:
    // Quien da la entrada: escribe n muestras estereo y avanza su propio cursor.
    struct Source
    {
        virtual ~Source() = default;
        virtual void read (float* left, float* right, int n) = 0;
    };

    // Tipo de motor: finer = Rubber Band R3 (mejor sonido, ~4 veces mas CPU); faster = R2.
    // Medido en un AMD A8-7410: R3 91 % de un nucleo por deck, R2 23 %.
    enum class Engine { finer, faster };

    TimeStretcher();
    ~TimeStretcher();

    // Fuera del hilo de audio: crea el estirador y reserva toda su memoria.
    void prepare (double sampleRate, Engine engine = Engine::faster);
    bool isPrepared() const { return stretcher != nullptr; }

    // Hilo de audio. Olvida lo acumulado (tras un salto o al activar keylock): la proxima
    // salida empieza exactamente en lo primero que de la Source, con un fundido corto.
    void reset();
    // tempo: 1.0 = original, 1.1 = un 10 % mas rapido.
    void setTempo (double tempo);
    // Hilo de audio: escribe n muestras en left/right pidiendo a 'source' lo que haga falta.
    void process (float* left, float* right, int n, Source& source);

    // Muestras de entrada que hay dentro del estirador y aun no han sonado (aprox.).
    int getLatencySamples() const;

private:
    static constexpr int chunkSize = 1024;   // entrada maxima por llamada a Rubber Band
    static constexpr int fadeLength = 256;   // fundido de entrada tras reset()

    void pull (Source& source);

    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher;
    juce::AudioBuffer<float> input, discard;
    int samplesToDiscard = 0;
    int fadePosition = fadeLength;
    double currentTempo = 1.0;
};
}
