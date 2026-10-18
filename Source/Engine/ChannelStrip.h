#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>

namespace midj
{
// Canal del mezclador para un deck: ganancia -> EQ de 3 bandas -> filtro -> volumen.
// Todos los mandos van de 0 a 1, como en VirtualDJ. La interfaz los cambia en cualquier
// momento (atomicos); el hilo de audio los lee y los suaviza para que no haya clics.
class ChannelStrip
{
public:
    static constexpr float defaultKnob = 0.5f;        // ganancia, EQ y filtro: centro
    static constexpr float defaultVolume = 1.0f;      // fader arriba
    static constexpr float lowCrossoverHz = 200.0f;   // frecuencias de corte de VirtualDJ
    static constexpr float highCrossoverHz = 6500.0f;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();   // borra el estado de los filtros y salta a los valores actuales
    // Procesa en el sitio las muestras [start, start + n) de los 2 primeros canales. preFader
    // (opcional, 2 canales, desde 0): la senal antes del volumen, para la pre-escucha.
    void process (juce::AudioBuffer<float>& buffer, int start, int n, juce::AudioBuffer<float>* preFader = nullptr);

    void setGain (float k)   { gainKnob = k; }
    void setLow (float k)    { lowKnob = k; }
    void setMid (float k)    { midKnob = k; }
    void setHigh (float k)   { highKnob = k; }
    void setFilter (float k) { filterKnob = k; }
    void setVolume (float v) { volumeFader = v; }
    float getGain() const    { return gainKnob.load(); }
    float getLow() const     { return lowKnob.load(); }
    float getMid() const     { return midKnob.load(); }
    float getHigh() const    { return highKnob.load(); }
    float getFilter() const  { return filterKnob.load(); }
    float getVolume() const  { return volumeFader.load(); }

    // Formulas de los mandos (publicas para las pruebas).
    // Ganancia: 0-0,5 -> x0 a x1; 0,5-1 -> x1 a x4 (+12 dB).
    static float gainFromKnob (float k);
    // Banda del EQ: 0 = corte total, 0,5 = plano, 1 = x2 (+6 dB).
    static float bandGainFromKnob (float k) { return 2.0f * juce::jlimit (0.0f, 1.0f, k); }
    // Volumen: curva cuadratica, mas resolucion en la parte alta del fader.
    static float volumeFromFader (float v)  { v = juce::jlimit (0.0f, 1.0f, v); return v * v; }

    struct FilterSetting
    {
        bool active = false;    // false = zona muerta, sin filtro
        bool lowPass = false;   // true = paso bajo (mando a la izquierda), false = paso alto
        float cutoffHz = 0.0f;
    };
    // Zona muerta 0,44-0,54. Izquierda: paso bajo en 20 + 15000 * (k / 0,44)^3 Hz.
    // Derecha: paso alto en 20 + 15000 * ((k - 0,54) / 0,46)^3 Hz.
    static FilterSetting filterFromKnob (float k);

private:
    void updateFilter (float knob);

    std::atomic<float> gainKnob { defaultKnob }, lowKnob { defaultKnob }, midKnob { defaultKnob },
                       highKnob { defaultKnob }, filterKnob { defaultKnob }, volumeFader { defaultVolume };

    double sampleRate = 44100.0;
    // Cruce de 3 vias Linkwitz-Riley: lowSplit separa graves / resto, highSplit separa
    // el resto en medios / agudos y lowAllpass da a los graves el mismo giro de fase
    // que sufren medios y agudos en 6500 Hz, para que las tres bandas sumen plano.
    juce::dsp::LinkwitzRileyFilter<float> lowSplit, highSplit, lowAllpass;
    juce::dsp::StateVariableTPTFilter<float> filter;
    bool filterActive = false;
    juce::SmoothedValue<float> gain, low, mid, high, volume, filterSmoothed;
};
}
