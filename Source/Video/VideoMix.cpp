#include "Video/VideoMix.h"

namespace midj
{
namespace
{
    // Ranuras de textura del compositor.
    enum Slot { videoA = 0, videoB = 1, cardA = 2, cardB = 3, logoSlot = 4 };
}

VideoMix::VideoMix (std::array<DeckVideo*, 2> v, std::array<const Deck*, 2> d, const Mixer& m)
    : videos (v), decks (d), mixer (m)
{
    logo = renderLogo (width, height);
}

void VideoMix::setProgramSize (int w, int h)
{
    width = w;
    height = h;
    logo = renderLogo (w, h);
    const juce::SpinLock::ScopedLockType sl (cardLock);
    for (auto& card : cards)
        card.title = "\x01";   // se vuelve a dibujar al siguiente setTitle
}

std::array<float, 2> VideoMix::weights (float crossfader, bool a, bool b)
{
    if (a && ! b) return { 1.0f, 0.0f };
    if (b && ! a) return { 0.0f, 1.0f };
    if (! a && ! b) return { 0.0f, 0.0f };
    const float x = juce::jlimit (0.0f, 1.0f, crossfader);
    return { 1.0f - x, x };
}

void VideoMix::setTitle (int d, const juce::String& title)
{
    const double bpm = title.isEmpty() ? 0.0 : std::round (decks[(size_t) d]->getCurrentBpm() * 10.0) / 10.0;
    {
        const juce::SpinLock::ScopedLockType sl (cardLock);
        const auto& card = cards[(size_t) d];
        if (card.title == title && card.bpm == bpm)
            return;
    }
    // Se dibuja fuera del cerrojo (unos milisegundos); el compositor sigue con el rotulo anterior.
    auto image = title.isEmpty() ? juce::Image() : renderCard (title, bpm, d, width, height);
    const juce::SpinLock::ScopedLockType sl (cardLock);
    auto& card = cards[(size_t) d];
    card.title = title;
    card.bpm = bpm;
    std::swap (card.image, image);
    ++card.version;
}

Compositor::Layer VideoMix::picture (int d)
{
    Compositor::Layer layer;
    auto* video = videos[(size_t) d];
    if (video->hasVideo())
    {
        layer.image = video->getFrame();
        layer.version = video->getFrameNumber();
        layer.slot = d == 0 ? videoA : videoB;
    }
    if (! layer.image.isValid())
    {
        // Sin video (o aun sin primer fotograma): el rotulo.
        const juce::SpinLock::ScopedLockType sl (cardLock);
        layer.image = cards[(size_t) d].image;
        layer.version = cards[(size_t) d].version;
        layer.slot = d == 0 ? cardA : cardB;
    }
    layer.opacity = getLevel (d);
    return layer;
}

Compositor::Layer VideoMix::logoLayer() const
{
    Compositor::Layer l;
    l.slot = logoSlot;
    l.image = logo;
    l.version = 1;
    return l;
}

void VideoMix::buildLayers (std::vector<Compositor::Layer>& layers)
{
    std::array<Compositor::Layer, 2> picture { this->picture (0), this->picture (1) };
    const std::array<bool, 2> has { picture[0].image.isValid(), picture[1].image.isValid() };

    const auto w = weights (mixer.getCrossfader(), has[0], has[1]);
    if (! has[0] && ! has[1])
    {
        auto l = logoLayer();
        l.area = { 0.0f, 0.0f, (float) width, (float) height };
        layers.push_back (std::move (l));
        return;
    }
    // Fundido cruzado: A debajo y B encima con su parte del total (en luz lineal, en la GPU).
    if (w[0] > 0.0f && w[1] > 0.0f)
    {
        picture[0].opacity = getLevel (0);
        picture[1].opacity = w[1] / (w[0] + w[1]) * getLevel (1);
        layers.push_back (std::move (picture[0]));
        layers.push_back (std::move (picture[1]));
    }
    else
    {
        auto& only = picture[w[0] > 0.0f ? 0 : 1];
        only.opacity = getLevel (w[0] > 0.0f ? 0 : 1);
        layers.push_back (std::move (only));
    }
}

juce::Image VideoMix::renderCard (const juce::String& title, double bpm, int d, int w, int h)
{
    // Cancion sin video: rotulo con el titulo (VirtualDJ muestra la caratula o el titulo).
    juce::Image image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    const auto area = image.getBounds().toFloat();
    const auto accent = d == 0 ? juce::Colour (0xff29b6f6) : juce::Colour (0xffffa726);
    g.setGradientFill (juce::ColourGradient (accent.darker (2.2f), area.getTopLeft(),
                                             juce::Colour (0xff050608), area.getBottomRight(), false));
    g.fillRect (area);
    const float fh = area.getHeight();
    auto text = area.reduced (area.getWidth() * 0.06f, fh * 0.1f);
    const auto artist = title.upToFirstOccurrenceOf (" - ", false, false);
    const auto name = title.contains (" - ") ? title.fromFirstOccurrenceOf (" - ", false, false) : title;
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (fh * 0.11f, juce::Font::bold));
    g.drawFittedText (name, text.removeFromTop (fh * 0.5f).toNearestInt(), juce::Justification::centredBottom, 2, 0.8f);
    if (title.contains (" - "))
    {
        g.setColour (accent.brighter (0.3f));
        g.setFont (juce::FontOptions (fh * 0.065f));
        g.drawFittedText (artist, text.removeFromTop (fh * 0.12f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
    if (bpm > 0.0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (juce::FontOptions (fh * 0.045f));
        g.drawFittedText (juce::String (bpm, 1) + " BPM", text.removeFromTop (fh * 0.1f).toNearestInt(), juce::Justification::centred, 1);
    }
    return image;
}

juce::Image VideoMix::renderLogo (int w, int h)
{
    juce::Image image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    juce::Graphics g (image);
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::white.withAlpha (0.2f));
    g.setFont (juce::FontOptions (h * 0.08f, juce::Font::bold));
    g.drawText ("MiDJ", image.getBounds(), juce::Justification::centred);
    return image;
}
}
