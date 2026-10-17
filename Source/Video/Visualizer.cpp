#include "Video/Visualizer.h"

namespace midj
{
Visualizer::Visualizer (const MasterTap& t) : tap (t)
{
}

float Visualizer::barEdge (int bar)
{
    return lowHz * std::pow (highHz / lowHz, (float) bar / (float) numBars);
}

void Visualizer::analyse (const float* in, double sampleRate, std::array<float, numBars>& out)
{
    std::copy (in, in + fftSize, fftData.begin());
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    // Un seno de amplitud 1 con ventana de Hann da N/4 en su bin: eso es 0 dB.
    const float fullScale = fftSize * 0.25f;
    const float binHz = (float) sampleRate / fftSize;
    const int lastBin = fftSize / 2 - 1;
    auto magnitudeAt = [&] (float hz)   // entre dos bins, interpolado (barras graves, mas finas que un bin)
    {
        const float b = juce::jlimit (0.0f, (float) lastBin, hz / binHz);
        const int i = juce::jmin ((int) b, lastBin - 1);
        return fftData[(size_t) i] + (fftData[(size_t) i + 1] - fftData[(size_t) i]) * (b - (float) i);
    };
    for (int bar = 0; bar < numBars; ++bar)
    {
        const float lo = barEdge (bar), hi = barEdge (bar + 1);
        const float centre = std::sqrt (lo * hi);
        float magnitude = magnitudeAt (centre);
        const int first = (int) std::ceil (lo / binHz), last = juce::jmin (lastBin, (int) std::floor (hi / binHz));
        for (int i = first; i <= last; ++i)
            magnitude = juce::jmax (magnitude, fftData[(size_t) i]);
        const float db = juce::Decibels::gainToDecibels (magnitude / fullScale, -100.0f) + 3.0f * std::log2 (centre / 1000.0f);
        out[(size_t) bar] = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    }
}

float Visualizer::level (const float* s, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += (double) s[i] * s[i];
    const float rms = n > 0 ? (float) std::sqrt (sum / n) : 0.0f;
    return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (rms, -100.0f) + 50.0f) / 50.0f);
}

int Visualizer::triggerPoint (const float* s, int n, int windowLength)
{
    const int latest = n - windowLength;
    // Hacia atras desde lo mas reciente, como mucho medio ciclo de 40 Hz (~1100 muestras).
    for (int i = latest; i > juce::jmax (0, latest - 1200); --i)
        if (s[i - 1] < 0.0f && s[i] >= 0.0f)
            return i;
    return juce::jmax (0, latest);
}

float Visualizer::beatFlash (double beat)
{
    const double whole = std::floor (beat);
    const float phase = (float) (beat - whole);
    const bool downbeat = juce::negativeAwareModulo ((int) whole, 4) == 0;
    return std::exp (-6.0f * phase) * (downbeat ? 1.0f : 0.75f);
}

juce::Point<int> Visualizer::renderSize (int w, int h)
{
    w = juce::jmax (1, w);
    h = juce::jmax (1, h);
    const float s = juce::jmin (1.0f, (float) maxRenderSide / (float) juce::jmax (w, h));
    return { juce::jmax (1, juce::roundToInt ((float) w * s)), juce::jmax (1, juce::roundToInt ((float) h * s)) };
}

const juce::Image& Visualizer::render (const SceneItem& item, double nowMs)
{
    const float dt = lastMs > 0.0 ? (float) juce::jlimit (0.0, 0.1, (nowMs - lastMs) * 0.001) : 0.0f;
    lastMs = nowMs;

    tap.read (samples.data(), fftSize, nowMs);
    const auto beat = tap.beatAt (nowMs);

    // Volumen y destello del pulso: con un deck sonando, el ritmo de su rejilla; si no, los golpes
    // de volumen.
    const float lv = level (samples.data() + fftSize - waveSamples, waveSamples);
    smoothLevel += (lv - smoothLevel) * juce::jmin (1.0f, dt * (lv > smoothLevel ? 25.0f : 6.0f));
    slowLevel += (lv - slowLevel) * juce::jmin (1.0f, dt * 1.5f);
    if (beat.valid)
        flash = beatFlash (beat.beat) * juce::jlimit (0.0f, 1.0f, lv * 1.5f);   // en silencio no late
    else if (lv > slowLevel + 0.08f)
        flash = 1.0f;
    else
        flash *= std::exp (-8.0f * dt);

    const auto size = renderSize (item.width, item.height);
    if (image.getWidth() != size.x || image.getHeight() != size.y)
        image = juce::Image (juce::Image::ARGB, size.x, size.y, true, juce::SoftwareImageType());
    else
        image.clear (image.getBounds());

    juce::Graphics g (image);
    const float w = (float) size.x, h = (float) size.y;
    switch (item.visualizer)
    {
        case VisualizerStyle::wave:  drawWave (g, item.colour, w, h); break;
        case VisualizerStyle::pulse: drawPulse (g, item.colour, w, h, beat); break;
        case VisualizerStyle::spectrum:
        default:
        {
            analyse (samples.data(), tap.getSampleRate(), target);
            for (size_t i = 0; i < (size_t) numBars; ++i)
            {
                // Sube rapido y baja despacio, como un vumetro; la marca de pico se queda un momento.
                bars[i] = target[i] > bars[i] ? bars[i] + (target[i] - bars[i]) * 0.7f : juce::jmax (target[i], bars[i] - dt * 1.5f);
                if (bars[i] >= peaks[i])
                {
                    peaks[i] = bars[i];
                    peakHold[i] = 0.35f;
                }
                else if ((peakHold[i] -= dt) < 0.0f)
                {
                    peaks[i] = juce::jmax (bars[i], peaks[i] - dt * 0.6f);
                }
            }
            drawSpectrum (g, item.colour, w, h);
            break;
        }
    }
    return image;
}

void Visualizer::drawSpectrum (juce::Graphics& g, juce::Colour colour, float w, float h)
{
    const float slot = w / numBars;
    const float gap = juce::jmax (1.0f, slot * 0.25f);
    const float capHeight = juce::jmax (2.0f, h * 0.015f);
    const auto top = colour.brighter (0.7f);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, 0.0f, colour.withMultipliedAlpha (0.85f), 0.0f, h, false));
    for (int i = 0; i < numBars; ++i)
    {
        const float bh = bars[(size_t) i] * (h - capHeight * 2.0f);
        if (bh >= 0.5f)
            g.fillRect (slot * (float) i + gap * 0.5f, h - bh, slot - gap, bh);
    }
    g.setColour (top.withMultipliedAlpha (0.6f + 0.4f * flash));
    for (int i = 0; i < numBars; ++i)
        if (peaks[(size_t) i] > 0.01f)
            g.fillRect (slot * (float) i + gap * 0.5f, h - peaks[(size_t) i] * (h - capHeight * 2.0f) - capHeight * 1.5f, slot - gap, capHeight);
}

void Visualizer::drawWave (juce::Graphics& g, juce::Colour colour, float w, float h)
{
    const int start = triggerPoint (samples.data(), fftSize, waveSamples);
    const int points = juce::jmax (2, (int) w);
    const float mid = h * 0.5f, amp = h * 0.45f;
    juce::Path path;
    for (int x = 0; x < points; ++x)
    {
        const int i = juce::jmin (fftSize - 1, start + x * waveSamples / points);
        const float y = mid - std::tanh (samples[(size_t) i] * 1.5f) * amp;   // limitada suave: no se sale
        if (x == 0)
            path.startNewSubPath (0.0f, y);
        else
            path.lineTo ((float) x * w / (float) (points - 1), y);
    }
    const float thickness = juce::jmax (1.5f, h * 0.012f);
    g.setColour (colour.withMultipliedAlpha (0.25f + 0.2f * flash));   // resplandor
    g.strokePath (path, juce::PathStrokeType (thickness * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (colour.brighter (0.3f + 0.4f * flash));
    g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void Visualizer::drawPulse (juce::Graphics& g, juce::Colour colour, float w, float h, const MasterTap::Beat& beat)
{
    const juce::Point<float> c (w * 0.5f, h * 0.5f);
    const float maxR = juce::jmin (w, h) * 0.5f;
    if (smoothLevel <= 0.01f && flash <= 0.01f)
        return;   // en silencio no se ve
    const float r = maxR * (0.35f + 0.3f * smoothLevel + 0.22f * flash);
    juce::ColourGradient fill (colour.brighter (0.6f + 0.4f * flash).withMultipliedAlpha (0.95f), c,
                               colour.withAlpha (0.0f), c.translated (r, 0.0f), true);
    fill.addColour (0.6, colour.withMultipliedAlpha (0.7f + 0.3f * flash));
    g.setGradientFill (fill);
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));

    // Con ritmo, un aro que sale del circulo en cada pulso y se apaga antes del siguiente.
    if (beat.valid)
    {
        const float phase = (float) (beat.beat - std::floor (beat.beat));
        const float ringR = juce::jmin (maxR * 0.98f, r * (1.0f + 0.7f * phase));
        g.setColour (colour.withMultipliedAlpha ((1.0f - phase) * 0.8f * juce::jlimit (0.0f, 1.0f, smoothLevel * 1.5f)));
        g.drawEllipse (juce::Rectangle<float> (ringR * 2.0f, ringR * 2.0f).withCentre (c), juce::jmax (1.5f, maxR * 0.03f));
    }
}
}
