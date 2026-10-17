#pragma once

#include "Engine/MasterTap.h"
#include "Video/Scenes.h"

#include <juce_dsp/juce_dsp.h>
#include <juce_graphics/juce_graphics.h>

namespace midj
{
// Visualizador de lo que suena por el master, como fuente de escena: espectro en barras, forma
// de onda o un circulo que late con el pulso del deck que manda. Se dibuja en la CPU en una
// imagen pequena (como mucho 640 px de lado) en el hilo del compositor, una vez por fotograma,
// y la tarjeta grafica la escala al tamano del elemento.
class Visualizer
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;   // 2048 muestras: ~46 ms a 44,1 kHz
    static constexpr int numBars = 48;
    static constexpr float lowHz = 40.0f, highHz = 16000.0f;
    static constexpr int maxRenderSide = 640;
    static constexpr int waveSamples = 1024;          // lo que se ve de la onda (~23 ms)

    explicit Visualizer (const MasterTap& tap);

    // Hilo del compositor: dibuja el fotograma del momento nowMs (getMillisecondCounterHiRes).
    // Devuelve siempre la misma imagen (ARGB premultiplicada), redibujada.
    const juce::Image& render (const SceneItem& item, double nowMs);

    // Analisis (publico para las pruebas).
    // Nivel de cada barra (0..1: de -60 a 0 dB, con +3 dB por octava sobre 1 kHz para que la
    // musica, que tiene mas graves, se vea pareja) de fftSize muestras.
    void analyse (const float* samples, double sampleRate, std::array<float, numBars>& out);
    // Frecuencia de los bordes de las barras (bar = 0..numBars): reparto logaritmico.
    static float barEdge (int bar);
    // Volumen (0..1: RMS de -50 a 0 dB).
    static float level (const float* samples, int n);
    // Donde empieza la onda que se ve: el cruce por cero hacia arriba mas reciente que deje
    // 'window' muestras detras (asi la onda no tiembla); si no hay, lo ultimo.
    static int triggerPoint (const float* samples, int n, int window);
    // Destello del pulso (0..1): 1 justo en el pulso y baja en el resto; mas en el primero del compas.
    static float beatFlash (double beat);

    // Tamano de la imagen que se dibuja para un elemento de w x h.
    static juce::Point<int> renderSize (int w, int h);

private:
    void drawSpectrum (juce::Graphics& g, juce::Colour colour, float w, float h);
    void drawWave (juce::Graphics& g, juce::Colour colour, float w, float h);
    void drawPulse (juce::Graphics& g, juce::Colour colour, float w, float h, const MasterTap::Beat& beat);

    const MasterTap& tap;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::array<float, 2 * fftSize> fftData {};
    std::array<float, fftSize> samples {};
    std::array<float, numBars> target {}, bars {}, peaks {}, peakHold {};
    float smoothLevel = 0.0f, slowLevel = 0.0f, flash = 0.0f;
    double lastMs = 0.0;
    juce::Image image;
};
}
