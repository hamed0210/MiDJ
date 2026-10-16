#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace midj
{
// Tempo que recibe cada efecto en cada bloque (como el SDK de VirtualDJ: muestras por pulso
// y posicion en pulsos). Sin BPM se usa un pulso de 0,5 s, igual que VirtualDJ.
struct TempoInfo
{
    double samplesPerBeat = 22050.0;
    double beatAtStart = 0.0;   // pulso (fraccionario) de la primera muestra del bloque
    bool synced = false;        // false = la cancion no tiene BPM: el pulso es libre
};

// Efecto de audio con un solo mando (0-1) y un boton ON, como los de VirtualDJ.
// La interfaz cambia on/amount (atomicos); el hilo de audio los suaviza: encender, apagar o
// mover el mando nunca hace clic. Apagado del todo, deja pasar la senal sin tocarla.
class Effect
{
public:
    virtual ~Effect() = default;
    virtual const char* getName() const = 0;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();   // vacia lineas de retardo y colas

    void setOn (bool shouldBeOn)  { on = shouldBeOn; }
    bool isOn() const             { return on.load(); }
    void setAmount (float value)  { amount = juce::jlimit (0.0f, 1.0f, value); }
    float getAmount() const       { return amount.load(); }

    // Hilo de audio. Procesa en el sitio n muestras estereo.
    void process (float* left, float* right, int n, const TempoInfo& tempo);
    // true si no esta haciendo nada (apagado, sin rampa ni cola): no gasta CPU.
    bool isIdle() const { return idle; }

protected:
    virtual void prepareEffect (double /*sampleRate*/, int /*maxBlockSize*/) {}
    virtual void resetEffect() {}
    virtual void processEffect (float* left, float* right, int n, const TempoInfo& tempo) = 0;
    // Efectos con cola (eco, reverb): siguen sonando un rato despues de apagarse.
    virtual bool hasTail() const { return false; }

    double sampleRate = 44100.0;
    juce::SmoothedValue<float> onLevel;         // 0 apagado, 1 encendido; rampa de 10 ms
    juce::SmoothedValue<float> amountSmoothed;  // mando suavizado; rampa de 50 ms

private:
    std::atomic<bool> on { false };
    std::atomic<float> amount { 0.5f };
    bool idle = true;
};

// Eco sincronizado: repite cada medio pulso. El mando sube la realimentacion (cuantas
// repeticiones) y el volumen del eco. Las repeticiones pasan por un paso alto y un paso bajo,
// cada vez mas apagadas, y al apagarlo la cola se extingue sola (umbral 0,005, como VirtualDJ).
class EchoEffect : public Effect
{
public:
    const char* getName() const override { return "Echo"; }
    static constexpr double beatsOfDelay = 0.5;

protected:
    void prepareEffect (double sampleRate, int maxBlockSize) override;
    void resetEffect() override;
    void processEffect (float* left, float* right, int n, const TempoInfo& tempo) override;
    bool hasTail() const override { return tailLevel > 0.005f; }

private:
    static constexpr int crossfadeLength = 64;   // al cambiar el tempo se funde de una toma a otra
    std::array<std::vector<float>, 2> buffer;
    int writePos = 0, delay = 0, previousDelay = 0, crossfade = 0;
    std::array<float, 2> lowState {}, highState {}, highInput {};
    float lowCoeff = 0.0f, highCoeff = 0.0f, tailLevel = 0.0f;
};

// Reverb (Freeverb de JUCE). El mando agranda la sala y sube la mezcla. Tiene cola.
class ReverbEffect : public Effect
{
public:
    const char* getName() const override { return "Reverb"; }

protected:
    void prepareEffect (double sampleRate, int maxBlockSize) override;
    void resetEffect() override;
    void processEffect (float* left, float* right, int n, const TempoInfo& tempo) override;
    bool hasTail() const override { return tailLevel > 0.0005f; }

private:
    juce::Reverb reverb;
    juce::AudioBuffer<float> wet;
    float lastAmount = -1.0f, tailLevel = 0.0f;
};

// Flanger: retardo corto (1-7 ms) que barre con un seno de 8 pulsos alineado a los compases.
// El mando da la profundidad del barrido y la realimentacion.
class FlangerEffect : public Effect
{
public:
    const char* getName() const override { return "Flanger"; }
    static constexpr double beatsPerSweep = 8.0;

protected:
    void prepareEffect (double sampleRate, int maxBlockSize) override;
    void resetEffect() override;
    void processEffect (float* left, float* right, int n, const TempoInfo& tempo) override;

private:
    std::array<std::vector<float>, 2> buffer;
    int writePos = 0;
    double freeBeat = 0.0;
};

// BitCrush con la formula de VirtualDJ: de 16 a 4 bits segun el cuadrado del mando y
// muestreo-retencion de 1 a 1024 muestras (normalizado a 44,1 kHz).
class BitCrushEffect : public Effect
{
public:
    const char* getName() const override { return "BitCrush"; }
    static float bitsFor (float amount)       { return 16.0f - 12.0f * amount * amount; }
    static float holdFactorFor (float amount) { return 1.0f + 1023.0f * amount * amount; }

protected:
    void resetEffect() override { held = { 0.0f, 0.0f }; holdCounter = 0.0; }
    void processEffect (float* left, float* right, int n, const TempoInfo& tempo) override;

private:
    std::array<float, 2> held {};
    double holdCounter = 0.0;
};

// Gate (trance gate): corta el volumen cada cuarto de pulso, en la segunda mitad de cada
// cuarto. El mando es cuanto baja (1 = silencio). Bordes de 1 ms para que no chasquee.
class GateEffect : public Effect
{
public:
    const char* getName() const override { return "Gate"; }
    static constexpr double stepsPerBeat = 4.0;

protected:
    void resetEffect() override { gain = 1.0f; freeBeat = 0.0; }
    void processEffect (float* left, float* right, int n, const TempoInfo& tempo) override;

private:
    float gain = 1.0f;
    double freeBeat = 0.0;
};

// Los efectos de un deck: 3 ranuras; cada una puede llevar cualquiera de los efectos.
// Al cambiar el efecto de una ranura, el viejo se apaga con su rampa (y su cola) y el nuevo
// se enciende con la suya: nunca se corta de golpe.
class EffectRack
{
public:
    static constexpr int numSlots = 3;
    static juce::StringArray getEffectNames();   // en el orden de los indices

    EffectRack();

    void prepare (double sampleRate, int maxBlockSize);
    void process (float* left, float* right, int n, const TempoInfo& tempo);   // hilo de audio

    void setEffect (int slot, int effectIndex);
    int getEffect (int slot) const { return selected[(size_t) slot].load(); }
    void setOn (int slot, bool on);
    bool isOn (int slot) const { return slotOn[(size_t) slot].load(); }
    void setAmount (int slot, float amount);
    float getAmount (int slot) const { return slotAmount[(size_t) slot].load(); }

private:
    static constexpr int numEffects = 5;
    std::array<std::array<std::unique_ptr<Effect>, numEffects>, numSlots> effects;
    std::array<std::atomic<int>, numSlots> selected;
    std::array<std::atomic<bool>, numSlots> slotOn;
    std::array<std::atomic<float>, numSlots> slotAmount;
};
}
