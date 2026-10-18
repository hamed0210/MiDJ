#include "UI/Icons.h"
#include "UI/DarkLookAndFeel.h"

namespace midj
{
namespace
{
    using R = juce::Rectangle<float>;

    // Cuadrado centrado en el area (los iconos se dibujan en un cuadro de 1 x 1 escalado).
    R square (R area)
    {
        const float s = juce::jmin (area.getWidth(), area.getHeight());
        return area.withSizeKeepingCentre (s, s);
    }

    void stroke (juce::Graphics& g, const juce::Path& p, float width)
    {
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void Icons::draw (juce::Graphics& g, R area, Glyph glyph, juce::Colour colour)
{
    const auto b = square (area).reduced (square (area).getWidth() * 0.12f);
    const float x = b.getX(), y = b.getY(), s = b.getWidth();
    const float w = juce::jmax (1.2f, s * 0.11f);   // grosor de linea
    auto at = [&] (float px, float py) { return juce::Point<float> (x + px * s, y + py * s); };
    g.setColour (colour);
    juce::Path p;
    switch (glyph)
    {
        case Glyph::plus:
            p.startNewSubPath (at (0.5f, 0.12f)); p.lineTo (at (0.5f, 0.88f));
            p.startNewSubPath (at (0.12f, 0.5f)); p.lineTo (at (0.88f, 0.5f));
            stroke (g, p, w);
            break;
        case Glyph::minus:
            p.startNewSubPath (at (0.15f, 0.5f)); p.lineTo (at (0.85f, 0.5f));
            stroke (g, p, w);
            break;
        case Glyph::trash:
            p.startNewSubPath (at (0.15f, 0.25f)); p.lineTo (at (0.85f, 0.25f));
            p.startNewSubPath (at (0.38f, 0.25f)); p.lineTo (at (0.42f, 0.12f)); p.lineTo (at (0.58f, 0.12f)); p.lineTo (at (0.62f, 0.25f));
            p.startNewSubPath (at (0.24f, 0.25f)); p.lineTo (at (0.3f, 0.9f)); p.lineTo (at (0.7f, 0.9f)); p.lineTo (at (0.76f, 0.25f));
            p.startNewSubPath (at (0.43f, 0.4f)); p.lineTo (at (0.43f, 0.75f));
            p.startNewSubPath (at (0.57f, 0.4f)); p.lineTo (at (0.57f, 0.75f));
            stroke (g, p, w * 0.85f);
            break;
        case Glyph::gear:
        {
            const auto c = at (0.5f, 0.5f);
            for (int i = 0; i < 8; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                p.startNewSubPath (c + juce::Point<float> (std::sin (a), -std::cos (a)) * (s * 0.28f));
                p.lineTo (c + juce::Point<float> (std::sin (a), -std::cos (a)) * (s * 0.45f));
            }
            stroke (g, p, w * 1.3f);
            g.drawEllipse (R (s * 0.56f, s * 0.56f).withCentre (c), w);
            g.drawEllipse (R (s * 0.2f, s * 0.2f).withCentre (c), w * 0.8f);
            break;
        }
        case Glyph::up:
        case Glyph::down:
        {
            const bool up = glyph == Glyph::up;
            p.startNewSubPath (at (0.18f, up ? 0.66f : 0.34f));
            p.lineTo (at (0.5f, up ? 0.3f : 0.7f));
            p.lineTo (at (0.82f, up ? 0.66f : 0.34f));
            stroke (g, p, w);
            break;
        }
        case Glyph::pencil:
            p.startNewSubPath (at (0.2f, 0.8f)); p.lineTo (at (0.25f, 0.6f)); p.lineTo (at (0.7f, 0.15f));
            p.lineTo (at (0.85f, 0.3f)); p.lineTo (at (0.4f, 0.75f)); p.closeSubPath();
            stroke (g, p, w * 0.85f);
            break;
        case Glyph::eye:
        case Glyph::eyeOff:
            p.startNewSubPath (at (0.06f, 0.5f));
            p.quadraticTo (at (0.5f, 0.05f), at (0.94f, 0.5f));
            p.quadraticTo (at (0.5f, 0.95f), at (0.06f, 0.5f));
            stroke (g, p, w * 0.85f);
            g.fillEllipse (R (s * 0.26f, s * 0.26f).withCentre (at (0.5f, 0.5f)));
            if (glyph == Glyph::eyeOff)
            {
                juce::Path slash;
                slash.startNewSubPath (at (0.12f, 0.88f));
                slash.lineTo (at (0.88f, 0.12f));
                stroke (g, slash, w);
            }
            break;
        case Glyph::lock:
        case Glyph::unlock:
        {
            g.fillRoundedRectangle (R (at (0.2f, 0.45f), at (0.8f, 0.9f)), s * 0.06f);
            const float right = glyph == Glyph::lock ? 0.68f : 0.9f;
            p.startNewSubPath (at (0.32f, 0.45f));
            p.lineTo (at (0.32f, 0.3f));
            p.quadraticTo (at (0.32f, 0.08f), at (0.5f, 0.08f));
            p.quadraticTo (at ((right + 0.32f) * 0.5f + 0.18f, 0.08f), at (right, 0.3f));
            if (glyph == Glyph::lock)
                p.lineTo (at (right, 0.45f));
            stroke (g, p, w);
            break;
        }
        case Glyph::play:
            p.addTriangle (at (0.25f, 0.12f), at (0.25f, 0.88f), at (0.85f, 0.5f));
            g.fillPath (p);
            break;
        case Glyph::pause:
            g.fillRect (R (at (0.22f, 0.15f), at (0.42f, 0.85f)));
            g.fillRect (R (at (0.58f, 0.15f), at (0.78f, 0.85f)));
            break;
        case Glyph::stop:
            g.fillRect (R (at (0.2f, 0.2f), at (0.8f, 0.8f)));
            break;
        case Glyph::restart:
        {
            const auto c = at (0.5f, 0.52f);
            p.addCentredArc (c.x, c.y, s * 0.32f, s * 0.32f, 0.0f, -2.2f, 2.6f, true);
            stroke (g, p, w);
            juce::Path head;
            const auto tip = c + juce::Point<float> (std::sin (-2.2f), -std::cos (-2.2f)) * (s * 0.32f);
            head.addTriangle (tip + juce::Point<float> (-s * 0.04f, -s * 0.2f), tip + juce::Point<float> (s * 0.18f, s * 0.04f),
                              tip + juce::Point<float> (-s * 0.12f, s * 0.12f));
            g.fillPath (head);
            break;
        }
        case Glyph::film:
        {
            const R frame (at (0.1f, 0.2f), at (0.9f, 0.8f));
            g.fillRoundedRectangle (frame, s * 0.06f);
            g.setColour (DarkLookAndFeel::background);
            for (int i = 0; i < 4; ++i)   // perforaciones arriba y abajo
            {
                const float px = 0.17f + (float) i * 0.19f;
                g.fillRect (R (at (px, 0.25f), at (px + 0.09f, 0.33f)));
                g.fillRect (R (at (px, 0.67f), at (px + 0.09f, 0.75f)));
            }
            juce::Path tri;
            tri.addTriangle (at (0.42f, 0.4f), at (0.42f, 0.6f), at (0.6f, 0.5f));
            g.fillPath (tri);
            break;
        }
        case Glyph::note:
            g.fillEllipse (R (at (0.18f, 0.62f), at (0.46f, 0.86f)));
            g.fillEllipse (R (at (0.56f, 0.52f), at (0.84f, 0.76f)));
            p.startNewSubPath (at (0.43f, 0.74f)); p.lineTo (at (0.43f, 0.2f)); p.lineTo (at (0.81f, 0.1f)); p.lineTo (at (0.81f, 0.64f));
            stroke (g, p, w);
            break;
        case Glyph::mix:   // dos caminos que se cruzan (mezclar ya)
            p.startNewSubPath (at (0.08f, 0.28f)); p.lineTo (at (0.28f, 0.28f));
            p.cubicTo (at (0.52f, 0.28f), at (0.5f, 0.72f), at (0.74f, 0.72f)); p.lineTo (at (0.9f, 0.72f));
            p.startNewSubPath (at (0.08f, 0.72f)); p.lineTo (at (0.28f, 0.72f));
            p.cubicTo (at (0.52f, 0.72f), at (0.5f, 0.28f), at (0.74f, 0.28f)); p.lineTo (at (0.9f, 0.28f));
            p.startNewSubPath (at (0.78f, 0.16f)); p.lineTo (at (0.9f, 0.28f)); p.lineTo (at (0.78f, 0.4f));
            p.startNewSubPath (at (0.78f, 0.6f)); p.lineTo (at (0.9f, 0.72f)); p.lineTo (at (0.78f, 0.84f));
            stroke (g, p, w);
            break;
        case Glyph::skip:   // siguiente
            p.addTriangle (at (0.18f, 0.16f), at (0.18f, 0.84f), at (0.66f, 0.5f));
            g.fillPath (p);
            g.fillRect (R (at (0.7f, 0.16f), at (0.82f, 0.84f)));
            break;
        case Glyph::folder:
        case Glyph::folderPlus:
            p.startNewSubPath (at (0.08f, 0.28f)); p.lineTo (at (0.38f, 0.28f)); p.lineTo (at (0.46f, 0.38f));
            p.lineTo (at (0.92f, 0.38f)); p.lineTo (at (0.92f, 0.84f)); p.lineTo (at (0.08f, 0.84f)); p.closeSubPath();
            if (glyph == Glyph::folder)
                g.fillPath (p);
            else
            {
                stroke (g, p, w * 0.9f);
                juce::Path plus;
                plus.startNewSubPath (at (0.5f, 0.48f)); plus.lineTo (at (0.5f, 0.74f));
                plus.startNewSubPath (at (0.37f, 0.61f)); plus.lineTo (at (0.63f, 0.61f));
                stroke (g, plus, w * 0.9f);
            }
            break;
        case Glyph::sync:   // dos flechas en circulo
        {
            const auto c = at (0.5f, 0.5f);
            const float r = s * 0.34f;
            p.addCentredArc (c.x, c.y, r, r, 0.0f, 0.35f, 2.8f, true);
            p.addCentredArc (c.x, c.y, r, r, 0.0f, 3.5f, 5.95f, true);
            stroke (g, p, w);
            for (const float a : { 2.8f, 5.95f })
            {
                const auto tip = c.getPointOnCircumference (r, a);
                juce::Path head;
                const auto along = juce::Point<float> (std::cos (a), std::sin (a)) * (s * 0.16f);   // tangente
                const auto out = juce::Point<float> (std::sin (a), -std::cos (a)) * (s * 0.1f);
                head.addTriangle (tip + along, tip - out, tip + out);
                g.fillPath (head);
            }
            break;
        }
        case Glyph::drive:
            g.drawRoundedRectangle (R (at (0.08f, 0.5f), at (0.92f, 0.84f)), s * 0.06f, w);
            p.startNewSubPath (at (0.14f, 0.5f)); p.lineTo (at (0.28f, 0.18f)); p.lineTo (at (0.72f, 0.18f)); p.lineTo (at (0.86f, 0.5f));
            stroke (g, p, w);
            g.fillEllipse (R (s * 0.1f, s * 0.1f).withCentre (at (0.74f, 0.67f)));
            break;
        case Glyph::computer:
        case Glyph::monitor:
            g.drawRoundedRectangle (R (at (0.08f, 0.14f), at (0.92f, 0.7f)), s * 0.06f, w);
            p.startNewSubPath (at (0.32f, 0.88f)); p.lineTo (at (0.68f, 0.88f));
            p.startNewSubPath (at (0.5f, 0.7f)); p.lineTo (at (0.5f, 0.88f));
            stroke (g, p, w);
            break;
        case Glyph::star:
            for (int i = 0; i < 10; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / 10.0f;
                const float r = (i % 2 == 0 ? 0.46f : 0.2f) * s;
                const auto pt = at (0.5f, 0.54f) + juce::Point<float> (std::sin (a), -std::cos (a)) * r;
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            p.closeSubPath();
            stroke (g, p, w * 0.85f);
            break;
        case Glyph::chevron:   // hacia la derecha (girarlo para abrir)
            p.startNewSubPath (at (0.36f, 0.18f)); p.lineTo (at (0.68f, 0.5f)); p.lineTo (at (0.36f, 0.82f));
            stroke (g, p, w * 1.2f);
            break;
        case Glyph::close:
            p.startNewSubPath (at (0.22f, 0.22f)); p.lineTo (at (0.78f, 0.78f));
            p.startNewSubPath (at (0.78f, 0.22f)); p.lineTo (at (0.22f, 0.78f));
            stroke (g, p, w);
            break;
        case Glyph::sliders:   // ajustes: tres lineas con su tirador
            for (int i = 0; i < 3; ++i)
            {
                const float py = 0.22f + (float) i * 0.28f, knob = std::array<float, 3> { 0.66f, 0.36f, 0.76f }[(size_t) i];
                p.startNewSubPath (at (0.08f, py)); p.lineTo (at (0.92f, py));
                g.fillEllipse (R (s * 0.2f, s * 0.2f).withCentre (at (knob, py)));
            }
            stroke (g, p, w * 0.9f);
            break;
        case Glyph::headphones:   // arco y dos almohadillas
            p.startNewSubPath (at (0.14f, 0.62f));
            p.cubicTo (at (0.14f, 0.1f), at (0.86f, 0.1f), at (0.86f, 0.62f));
            stroke (g, p, w);
            g.fillRoundedRectangle (R (at (0.08f, 0.55f), at (0.3f, 0.9f)), s * 0.06f);
            g.fillRoundedRectangle (R (at (0.7f, 0.55f), at (0.92f, 0.9f)), s * 0.06f);
            break;
    }
}

void Icons::drawSource (juce::Graphics& g, R area, SourceType type, juce::Colour colour)
{
    const auto b = square (area).reduced (square (area).getWidth() * 0.1f);
    const float x = b.getX(), y = b.getY(), s = b.getWidth();
    const float w = juce::jmax (1.2f, s * 0.1f);
    auto at = [&] (float px, float py) { return juce::Point<float> (x + px * s, y + py * s); };
    g.setColour (colour);
    juce::Path p;
    switch (type)
    {
        case SourceType::screen:   // monitor
            g.drawRoundedRectangle (R (at (0.08f, 0.15f), at (0.92f, 0.72f)), s * 0.06f, w);
            p.startNewSubPath (at (0.35f, 0.88f)); p.lineTo (at (0.65f, 0.88f));
            p.startNewSubPath (at (0.5f, 0.72f)); p.lineTo (at (0.5f, 0.88f));
            stroke (g, p, w);
            break;
        case SourceType::window:   // ventana con su barra
            g.drawRect (R (at (0.1f, 0.15f), at (0.9f, 0.85f)), w);
            g.fillRect (R (at (0.1f, 0.15f), at (0.9f, 0.32f)));
            break;
        case SourceType::captureDevice:   // camara
            g.fillRoundedRectangle (R (at (0.06f, 0.28f), at (0.68f, 0.76f)), s * 0.06f);
            p.addTriangle (at (0.7f, 0.52f), at (0.94f, 0.3f), at (0.94f, 0.74f));
            g.fillPath (p);
            break;
        case SourceType::image:   // marco con montana y sol
            g.drawRect (R (at (0.08f, 0.15f), at (0.92f, 0.85f)), w);
            p.startNewSubPath (at (0.14f, 0.8f)); p.lineTo (at (0.4f, 0.45f)); p.lineTo (at (0.58f, 0.65f));
            p.lineTo (at (0.7f, 0.52f)); p.lineTo (at (0.86f, 0.8f)); p.closeSubPath();
            g.fillPath (p);
            g.fillEllipse (R (at (0.62f, 0.24f), at (0.76f, 0.38f)));
            break;
        case SourceType::youtube:   // rectangulo redondeado con el triangulo de play
            g.fillRoundedRectangle (R (at (0.06f, 0.2f), at (0.94f, 0.8f)), s * 0.16f);
            p.addTriangle (at (0.42f, 0.36f), at (0.42f, 0.64f), at (0.66f, 0.5f));
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.fillPath (p);
            break;
        case SourceType::media:
            Icons::draw (g, area, Icons::Glyph::play, colour);
            break;
        case SourceType::text:
            g.setFont (juce::FontOptions (s * 0.95f, juce::Font::bold));
            g.drawText ("T", b, juce::Justification::centred, false);
            break;
        case SourceType::colour:
            g.fillRoundedRectangle (R (at (0.15f, 0.15f), at (0.85f, 0.85f)), s * 0.08f);
            break;
        case SourceType::visualizer:   // barras de espectro
            for (int i = 0; i < 5; ++i)
            {
                const float h = std::array<float, 5> { 0.45f, 0.75f, 0.55f, 0.85f, 0.35f }[(size_t) i];
                const float px = 0.1f + (float) i * 0.17f;
                g.fillRect (R (at (px, 0.9f - h), at (px + 0.12f, 0.9f)));
            }
            break;
        case SourceType::decks:   // dos platos
        case SourceType::deckA:
        case SourceType::deckB:
        {
            const bool both = type == SourceType::decks;
            for (int d = 0; d < 2; ++d)
            {
                if (! both && d != (type == SourceType::deckA ? 0 : 1))
                    continue;
                const auto c = both ? at (d == 0 ? 0.28f : 0.72f, 0.5f) : at (0.5f, 0.5f);
                const float r = both ? s * 0.22f : s * 0.38f;
                g.drawEllipse (R (r * 2.0f, r * 2.0f).withCentre (c), w);
                g.fillEllipse (R (r * 0.5f, r * 0.5f).withCentre (c));
            }
            if (! both)
            {
                g.setFont (juce::FontOptions (s * 0.32f, juce::Font::bold));
                g.drawText (type == SourceType::deckA ? "A" : "B", R (at (0.62f, 0.62f), at (1.0f, 1.0f)), juce::Justification::centred, false);
            }
            break;
        }
    }
}

void Icons::drawFileType (juce::Graphics& g, R area, bool video)
{
    // Video en el color del deck A (como un fotograma), audio en gris (una nota).
    draw (g, area, video ? Glyph::film : Glyph::note, video ? DarkLookAndFeel::accentA.brighter (0.2f) : DarkLookAndFeel::textQuiet);
}

// --- Boton -------------------------------------------------------------------------------------

IconButton::IconButton (Icons::Glyph g, const juce::String& tooltip) : juce::Button (tooltip), glyph (g)
{
    setTooltip (tooltip);
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float corner = bordered ? 8.0f : 4.0f;
    const bool filled = ! fill.isTransparent();
    if (filled)
    {
        g.setColour (fill.withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
        g.fillRoundedRectangle (r, corner);
    }
    else if (bordered)
    {
        g.setColour (DarkLookAndFeel::line.withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    }
    if (highlighted || down || getToggleState())
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.18f : 0.08f));
        g.fillRoundedRectangle (r, corner);
    }
    const auto colour = ! isEnabled() ? DarkLookAndFeel::textQuiet.withAlpha (0.4f)
                      : filled ? DarkLookAndFeel::background
                      : getToggleState() ? DarkLookAndFeel::accentA.brighter (0.3f) : DarkLookAndFeel::text;
    const float size = juce::jmin (r.getWidth(), r.getHeight()) * (bordered ? 0.55f : 0.7f);
    Icons::draw (g, r.withSizeKeepingCentre (juce::jmin (size, 22.0f), juce::jmin (size, 22.0f)), glyph, colour);
}
}
