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

juce::Point<int> Visualizer::renderSize (int w, int h, int maxSide)
{
    w = juce::jmax (1, w);
    h = juce::jmax (1, h);
    const float s = juce::jmin (1.0f, (float) maxSide / (float) juce::jmax (w, h));
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

    // El pulso es un degradado: con menos pixeles se ve igual.
    const auto size = renderSize (item.width, item.height, item.visualizer == VisualizerStyle::pulse ? pulseRenderSide : maxRenderSide);
    if (image.getWidth() != size.x || image.getHeight() != size.y)
        image = juce::Image (juce::Image::ARGB, size.x, size.y, true, juce::SoftwareImageType());
    else
        image.clear (image.getBounds());

    // Se escribe directamente en los pixeles (premultiplicados): trazar con juce::Graphics costaba
    // hasta media CPU de este equipo con la onda.
    juce::Image::BitmapData data (image, juce::Image::BitmapData::readWrite);
    switch (item.visualizer)
    {
        case VisualizerStyle::wave:  drawWave (data, item.colour); break;
        case VisualizerStyle::pulse: drawPulse (data, item.colour, beat); break;
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
            drawSpectrum (data, item.colour);
            break;
        }
    }
    return image;
}

namespace
{
    using Pixel = juce::PixelARGB;

    Pixel premultiplied (juce::Colour c, float alpha = 1.0f)
    {
        return c.withMultipliedAlpha (juce::jlimit (0.0f, 1.0f, alpha)).getPixelARGB();
    }

    Pixel scaled (Pixel p, float coverage)
    {
        p.multiplyAlpha (juce::jlimit (0.0f, 1.0f, coverage));
        return p;
    }

    // Una columna de x desde top hasta bottom (en pixeles, con borde suave arriba y abajo),
    // encima de lo que ya hay.
    void column (juce::Image::BitmapData& d, int x, float top, float bottom, Pixel colour)
    {
        top = juce::jmax (0.0f, top);
        bottom = juce::jmin ((float) d.height, bottom);
        if (bottom <= top || x < 0 || x >= d.width)
            return;
        const int first = (int) top, last = juce::jmin (d.height - 1, (int) std::ceil (bottom) - 1);
        for (int y = first; y <= last; ++y)
        {
            const float coverage = juce::jmin ((float) y + 1.0f, bottom) - juce::jmax ((float) y, top);
            auto* p = reinterpret_cast<Pixel*> (d.getPixelPointer (x, y));
            p->blend (coverage >= 0.999f ? colour : scaled (colour, coverage));
        }
    }
}

void Visualizer::drawSpectrum (juce::Image::BitmapData& d, juce::Colour colour)
{
    const float w = (float) d.width, h = (float) d.height;
    // Degradado vertical (claro arriba): un color por fila, calculado una vez por fotograma.
    const auto top = colour.brighter (0.7f);
    rows.resize ((size_t) d.height);
    for (int y = 0; y < d.height; ++y)
        rows[(size_t) y] = premultiplied (top.interpolatedWith (colour.withMultipliedAlpha (0.85f), (float) y / juce::jmax (1.0f, h - 1.0f)));
    const auto cap = premultiplied (top, 0.6f + 0.4f * flash);

    const float slot = w / numBars;
    const float gap = juce::jmax (1.0f, slot * 0.25f);
    const float capHeight = juce::jmax (2.0f, h * 0.015f);
    const float usable = h - capHeight * 2.0f;
    for (int i = 0; i < numBars; ++i)
    {
        const int x0 = juce::roundToInt (slot * (float) i + gap * 0.5f);
        const int x1 = juce::roundToInt (slot * (float) (i + 1) - gap * 0.5f);
        const float barTop = h - bars[(size_t) i] * usable;
        if (h - barTop >= 0.5f)
        {
            const int first = (int) barTop;
            for (int y = first; y < d.height; ++y)
            {
                const auto c = y == first ? scaled (rows[(size_t) y], (float) (first + 1) - barTop) : rows[(size_t) y];
                auto* p = reinterpret_cast<Pixel*> (d.getPixelPointer (x0, y));
                for (int x = x0; x < x1; ++x, p = juce::addBytesToPointer (p, d.pixelStride))
                    *p = c;   // la imagen esta vacia: no hace falta mezclar
            }
        }
        if (peaks[(size_t) i] > 0.01f)
        {
            const float capTop = h - peaks[(size_t) i] * usable - capHeight * 1.5f;
            for (int x = x0; x < x1; ++x)
                column (d, x, capTop, capTop + capHeight, cap);
        }
    }
}

void Visualizer::drawWave (juce::Image::BitmapData& d, juce::Colour colour)
{
    const int start = triggerPoint (samples.data(), fftSize, waveSamples);
    const float h = (float) d.height;
    const float mid = h * 0.5f, amp = h * 0.45f;
    const float thickness = juce::jmax (1.5f, h * 0.012f);
    const auto glow = premultiplied (colour, 0.25f + 0.2f * flash);
    const auto line = premultiplied (colour.brighter (0.3f + 0.4f * flash));
    auto yAt = [&] (int x)
    {
        const int i = juce::jmin (fftSize - 1, start + x * waveSamples / juce::jmax (1, d.width));
        return mid - std::tanh (samples[(size_t) i] * 1.5f) * amp;   // limitada suave: no se sale
    };
    // Cada columna cubre el tramo desde el punto anterior hasta el suyo (asi la linea no se corta
    // cuando sube o baja deprisa), mas el grosor; primero el resplandor ancho y luego la linea.
    float previous = yAt (0);
    for (int x = 0; x < d.width; ++x)
    {
        const float y = yAt (x);
        const float lo = juce::jmin (previous, y), hi = juce::jmax (previous, y);
        column (d, x, lo - thickness * 2.0f, hi + thickness * 2.0f, glow);
        column (d, x, lo - thickness * 0.5f, hi + thickness * 0.5f, line);
        previous = y;
    }
}

void Visualizer::drawPulse (juce::Image::BitmapData& d, juce::Colour colour, const MasterTap::Beat& beat)
{
    if (smoothLevel <= 0.01f && flash <= 0.01f)
        return;   // en silencio no se ve
    const float w = (float) d.width, h = (float) d.height;
    const float cx = w * 0.5f, cy = h * 0.5f;
    const float maxR = juce::jmin (w, h) * 0.5f;
    const float r = maxR * (0.35f + 0.3f * smoothLevel + 0.22f * flash);

    // Relleno: claro en el centro, el color a 0,6 del radio y transparente en el borde (tabla de
    // 256 colores por fotograma en lugar de un degradado por pixel).
    const auto centre = colour.brighter (0.6f + 0.4f * flash).withMultipliedAlpha (0.95f);
    const auto middle = colour.withMultipliedAlpha (0.7f + 0.3f * flash);
    std::array<Pixel, 256> fill;
    for (int i = 0; i < 256; ++i)
    {
        const float t = (float) i / 255.0f;
        fill[(size_t) i] = t < 0.6f ? premultiplied (centre.interpolatedWith (middle, t / 0.6f))
                                    : premultiplied (middle.interpolatedWith (colour.withAlpha (0.0f), (t - 0.6f) / 0.4f));
    }

    // Con ritmo, un aro que sale del circulo en cada pulso y se apaga antes del siguiente.
    float ringR = 0.0f, ringHalf = 0.0f;
    Pixel ring {};
    if (beat.valid)
    {
        const float phase = (float) (beat.beat - std::floor (beat.beat));
        ringR = juce::jmin (maxR * 0.98f - 1.0f, r * (1.0f + 0.7f * phase));
        ringHalf = juce::jmax (0.75f, maxR * 0.015f);
        ring = premultiplied (colour, (1.0f - phase) * 0.8f * juce::jlimit (0.0f, 1.0f, smoothLevel * 1.5f));
    }

    const float reach = juce::jmax (r, ringR + ringHalf + 1.0f);
    const int x0 = juce::jmax (0, (int) (cx - reach)), x1 = juce::jmin (d.width, (int) std::ceil (cx + reach));
    const int y0 = juce::jmax (0, (int) (cy - reach)), y1 = juce::jmin (d.height, (int) std::ceil (cy + reach));
    for (int y = y0; y < y1; ++y)
    {
        const float dy = (float) y + 0.5f - cy;
        auto* p = reinterpret_cast<Pixel*> (d.getPixelPointer (x0, y));
        for (int x = x0; x < x1; ++x, p = juce::addBytesToPointer (p, d.pixelStride))
        {
            const float dx = (float) x + 0.5f - cx;
            const float dist = std::sqrt (dx * dx + dy * dy);
            if (dist < r)
                *p = fill[(size_t) (dist / r * 255.0f)];
            if (ringHalf > 0.0f)
            {
                const float coverage = ringHalf + 0.5f - std::abs (dist - ringR);
                if (coverage > 0.0f)
                    p->blend (scaled (ring, coverage));
            }
        }
    }
}
}
