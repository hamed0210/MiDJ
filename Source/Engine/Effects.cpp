#include "Engine/Effects.h"

#include <cmath>

namespace midj
{
namespace
{
    constexpr double maxEchoSeconds = 4.0;   // medio pulso a 8 BPM: de sobra
    constexpr double flangerBufferSeconds = 0.02;
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    // Lectura con interpolacion lineal de una linea de retardo circular.
    inline float readDelay (const std::vector<float>& buf, int writePos, double delaySamples)
    {
        const int size = (int) buf.size();
        double pos = writePos - delaySamples;
        while (pos < 0.0)
            pos += size;
        const int i = (int) pos;
        const float t = (float) (pos - i);
        return buf[(size_t) i] * (1.0f - t) + buf[(size_t) ((i + 1) % size)] * t;
    }

    // Coeficiente de un filtro de un polo para una frecuencia de corte.
    inline float onePole (double cutoffHz, double sampleRate)
    {
        return (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * cutoffHz / sampleRate));
    }

    inline double fraction (double x) { return x - std::floor (x); }   // tambien para x negativos

    inline double beatAt (const TempoInfo& tempo, double freeBeat, int i)
    {
        return (tempo.synced ? tempo.beatAtStart : freeBeat) + i / tempo.samplesPerBeat;
    }
}

// --- Effect -------------------------------------------------------------------------------

void Effect::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr;
    onLevel.reset (sr, 0.01);
    amountSmoothed.reset (sr, 0.05);
    prepareEffect (sr, maxBlockSize);
    reset();
}

void Effect::reset()
{
    onLevel.setCurrentAndTargetValue (on.load() ? 1.0f : 0.0f);
    amountSmoothed.setCurrentAndTargetValue (amount.load());
    resetEffect();
    idle = ! on.load();
}

void Effect::process (float* left, float* right, int n, const TempoInfo& tempo)
{
    const bool wantOn = on.load();
    onLevel.setTargetValue (wantOn ? 1.0f : 0.0f);
    amountSmoothed.setTargetValue (amount.load());

    if (! wantOn && ! onLevel.isSmoothing() && ! hasTail())
    {
        if (! idle)
            resetEffect();   // al terminar: lineas de retardo limpias para la proxima vez
        idle = true;
        return;              // apagado: la senal pasa intacta
    }
    idle = false;
    processEffect (left, right, n, tempo);
}

// --- Echo ---------------------------------------------------------------------------------

void EchoEffect::prepareEffect (double sr, int)
{
    for (auto& b : buffer)
        b.assign ((size_t) (maxEchoSeconds * sr), 0.0f);
    lowCoeff = onePole (6000.0, sr);    // las repeticiones pierden brillo...
    highCoeff = onePole (150.0, sr);    // ...y graves, para no embarrar la mezcla
}

void EchoEffect::resetEffect()
{
    for (auto& b : buffer)
        std::fill (b.begin(), b.end(), 0.0f);
    writePos = 0;
    delay = previousDelay = 0;
    crossfade = 0;
    lowState = highState = highInput = {};
    tailLevel = 0.0f;
}

void EchoEffect::processEffect (float* left, float* right, int n, const TempoInfo& tempo)
{
    const int size = (int) buffer[0].size();
    const int target = juce::jlimit (1, size - 1, (int) std::lround (beatsOfDelay * tempo.samplesPerBeat));
    if (delay == 0)
        delay = previousDelay = target;
    else if (target != delay && crossfade == 0)
    {
        previousDelay = delay;   // el tempo cambio: fundido de 64 muestras a la toma nueva
        delay = target;
        crossfade = crossfadeLength;
    }

    // El detector de cola baja a la mitad en cada repeticion: entre dos ecos (hasta medio pulso
    // de silencio) no puede dar la cola por terminada.
    const float tailDecay = std::pow (0.5f, 1.0f / (float) delay);
    float* io[2] = { left, right };
    for (int i = 0; i < n; ++i)
    {
        const float send = onLevel.getNextValue();
        const float a = amountSmoothed.getNextValue();
        const float feedback = 0.3f + 0.55f * a;   // 0,30 - 0,85
        const float level = 0.35f + 0.65f * a;
        const float fade = crossfade > 0 ? (float) crossfade / crossfadeLength : 0.0f;
        float peak = 0.0f;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& buf = buffer[(size_t) ch];
            float tap = buf[(size_t) ((writePos - delay + size) % size)];
            if (fade > 0.0f)
                tap = tap * (1.0f - fade) + buf[(size_t) ((writePos - previousDelay + size) % size)] * fade;

            // Realimentacion filtrada: paso bajo y despues paso alto (de un polo).
            lowState[(size_t) ch] += lowCoeff * (tap - lowState[(size_t) ch]);
            highState[(size_t) ch] += highCoeff * (lowState[(size_t) ch] - highState[(size_t) ch]);
            const float filtered = lowState[(size_t) ch] - highState[(size_t) ch];

            const float x = io[ch][i];
            buf[(size_t) writePos] = x * send + filtered * feedback;
            io[ch][i] = x + tap * level;   // el eco se suma siempre: asi la cola sigue al apagar
            peak = std::max (peak, std::abs (tap));
        }
        tailLevel = std::max (peak, tailLevel * tailDecay);
        if (crossfade > 0)
            --crossfade;
        writePos = (writePos + 1) % size;
    }
}

// --- Reverb -------------------------------------------------------------------------------

void ReverbEffect::prepareEffect (double sr, int maxBlockSize)
{
    reverb.setSampleRate (sr);
    wet.setSize (2, juce::jmax (1, maxBlockSize));
}

void ReverbEffect::resetEffect()
{
    reverb.reset();
    lastAmount = -1.0f;
    tailLevel = 0.0f;
}

void ReverbEffect::processEffect (float* left, float* right, int n, const TempoInfo&)
{
    for (int done = 0; done < n;)
    {
        const int k = juce::jmin (n - done, wet.getNumSamples());
        float* wl = wet.getWritePointer (0);
        float* wr = wet.getWritePointer (1);

        // Lo que entra en la reverb lleva la rampa de encendido; la cola sale siempre.
        float a = amountSmoothed.getCurrentValue();
        for (int i = 0; i < k; ++i)
        {
            const float send = onLevel.getNextValue();
            a = amountSmoothed.getNextValue();
            wl[i] = left[done + i] * send;
            wr[i] = right[done + i] * send;
        }
        if (std::abs (a - lastAmount) > 0.01f)
        {
            juce::Reverb::Parameters p;
            p.roomSize = 0.5f + 0.45f * a;
            p.damping = 0.4f;
            p.wetLevel = 1.0f;
            p.dryLevel = 0.0f;
            p.width = 1.0f;
            reverb.setParameters (p);
            lastAmount = a;
        }
        reverb.processStereo (wl, wr, k);

        const float level = 0.15f + 0.45f * a;
        float peak = 0.0f;
        for (int i = 0; i < k; ++i)
        {
            left[done + i] += wl[i] * level;
            right[done + i] += wr[i] * level;
            peak = std::max ({ peak, std::abs (wl[i]), std::abs (wr[i]) });
        }
        tailLevel = std::max (peak, tailLevel * 0.5f);
        done += k;
    }
}

// --- Flanger ------------------------------------------------------------------------------

void FlangerEffect::prepareEffect (double sr, int)
{
    for (auto& b : buffer)
        b.assign ((size_t) (flangerBufferSeconds * sr), 0.0f);
}

void FlangerEffect::resetEffect()
{
    for (auto& b : buffer)
        std::fill (b.begin(), b.end(), 0.0f);
    writePos = 0;
}

void FlangerEffect::processEffect (float* left, float* right, int n, const TempoInfo& tempo)
{
    const int size = (int) buffer[0].size();
    float* io[2] = { left, right };
    for (int i = 0; i < n; ++i)
    {
        const float mix = onLevel.getNextValue();
        const float a = amountSmoothed.getNextValue();
        // Seno de 8 pulsos: en el pulso 0 de cada 8 el retardo es minimo.
        const double phase = fraction (beatAt (tempo, freeBeat, i) / beatsPerSweep);
        const float lfo = 0.5f - 0.5f * std::cos (twoPi * (float) phase);
        const double delayMs = 1.0 + 6.0 * a * lfo;
        const double delaySamples = delayMs * 0.001 * sampleRate;
        const float feedback = 0.3f + 0.4f * a;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& buf = buffer[(size_t) ch];
            const float tap = readDelay (buf, writePos, delaySamples);
            const float x = io[ch][i];
            buf[(size_t) writePos] = x + tap * feedback;
            const float flanged = 0.5f * (x + tap);
            io[ch][i] = x + (flanged - x) * mix;
        }
        writePos = (writePos + 1) % size;
    }
    if (! tempo.synced)
        freeBeat += n / tempo.samplesPerBeat;
}

// --- BitCrush -----------------------------------------------------------------------------

void BitCrushEffect::processEffect (float* left, float* right, int n, const TempoInfo&)
{
    float* io[2] = { left, right };
    for (int i = 0; i < n; ++i)
    {
        const float mix = onLevel.getNextValue();
        const float a = amountSmoothed.getNextValue();
        const float levels = std::exp2 (bitsFor (a) - 1.0f);
        const double hold = holdFactorFor (a) * sampleRate / 44100.0;

        if (holdCounter <= 0.0)
        {
            held = { io[0][i], io[1][i] };   // nueva muestra retenida
            holdCounter += hold;
        }
        holdCounter -= 1.0;

        for (int ch = 0; ch < 2; ++ch)
        {
            const float crushed = std::round (held[(size_t) ch] * levels) / levels;
            const float x = io[ch][i];
            io[ch][i] = x + (crushed - x) * mix;
        }
    }
}

// --- Gate ---------------------------------------------------------------------------------

void GateEffect::processEffect (float* left, float* right, int n, const TempoInfo& tempo)
{
    const float smoothing = onePole (160.0, sampleRate);   // constante de tiempo de ~1 ms
    for (int i = 0; i < n; ++i)
    {
        const float mix = onLevel.getNextValue();
        const float a = amountSmoothed.getNextValue();
        const double step = fraction (beatAt (tempo, freeBeat, i) * stepsPerBeat);
        const bool open = step < 0.5;   // primera mitad de cada cuarto de pulso
        const float target = open ? 1.0f : 1.0f - a;
        gain += smoothing * (target - gain);
        const float g = 1.0f + (gain - 1.0f) * mix;
        left[i] *= g;
        right[i] *= g;
    }
    if (! tempo.synced)
        freeBeat += n / tempo.samplesPerBeat;
}

// --- EffectRack ---------------------------------------------------------------------------

juce::StringArray EffectRack::getEffectNames()
{
    return { "Echo", "Reverb", "Flanger", "BitCrush", "Gate" };
}

EffectRack::EffectRack()
{
    const std::array<int, numSlots> defaults { 0, 2, 4 };   // Echo, Flanger, Gate
    for (size_t s = 0; s < (size_t) numSlots; ++s)
    {
        effects[s] = { std::make_unique<EchoEffect>(), std::make_unique<ReverbEffect>(),
                       std::make_unique<FlangerEffect>(), std::make_unique<BitCrushEffect>(),
                       std::make_unique<GateEffect>() };
        selected[s] = defaults[s];
        slotOn[s] = false;
        slotAmount[s] = 0.5f;
    }
}

void EffectRack::prepare (double sampleRate, int maxBlockSize)
{
    for (auto& slot : effects)
        for (auto& effect : slot)
            effect->prepare (sampleRate, maxBlockSize);
}

void EffectRack::process (float* left, float* right, int n, const TempoInfo& tempo)
{
    // En cada ranura suena el efecto elegido y, mientras se apaga, el anterior.
    for (auto& slot : effects)
        for (auto& effect : slot)
            effect->process (left, right, n, tempo);
}

void EffectRack::setEffect (int slot, int effectIndex)
{
    effectIndex = juce::jlimit (0, numEffects - 1, effectIndex);
    const auto s = (size_t) slot;
    const int old = selected[s].exchange (effectIndex);
    if (old == effectIndex)
        return;
    effects[s][(size_t) old]->setOn (false);
    effects[s][(size_t) effectIndex]->setAmount (slotAmount[s].load());
    effects[s][(size_t) effectIndex]->setOn (slotOn[s].load());
}

void EffectRack::setOn (int slot, bool on)
{
    slotOn[(size_t) slot] = on;
    effects[(size_t) slot][(size_t) selected[(size_t) slot].load()]->setOn (on);
}

void EffectRack::setAmount (int slot, float amount)
{
    slotAmount[(size_t) slot] = amount;
    effects[(size_t) slot][(size_t) selected[(size_t) slot].load()]->setAmount (amount);
}
}
