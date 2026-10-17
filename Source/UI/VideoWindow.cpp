#include "UI/VideoWindow.h"

namespace midj
{
VideoOutput::VideoOutput (std::array<DeckVideo*, 2> v, std::array<const Deck*, 2> d, const Mixer& m)
    : videos (v), decks (d), mixer (m)
{
    setOpaque (true);
    startTimerHz (60);   // solo se repinta si hay fotograma nuevo o se mueve el crossfader
}

std::array<float, 2> VideoOutput::weights (float crossfader, bool a, bool b)
{
    if (a && ! b) return { 1.0f, 0.0f };
    if (b && ! a) return { 0.0f, 1.0f };
    if (! a && ! b) return { 0.0f, 0.0f };
    const float x = juce::jlimit (0.0f, 1.0f, crossfader);
    return { 1.0f - x, x };
}

void VideoOutput::timerCallback()
{
    bool changed = false;
    for (int d = 0; d < 2; ++d)
    {
        const double t = videos[(size_t) d]->hasVideo() ? videos[(size_t) d]->getFrameTime() : -1.0;
        const auto title = getTitle != nullptr ? getTitle (d) : juce::String();
        if (t != shownTimes[(size_t) d] || title != shownTitles[(size_t) d])
        {
            shownTimes[(size_t) d] = t;
            shownTitles[(size_t) d] = title;
            changed = true;
        }
    }
    const float x = mixer.getCrossfader();
    if (std::abs (x - shownCrossfader) > 0.002f)
    {
        shownCrossfader = x;
        changed = true;
    }
    if (changed)
        repaint();
}

void VideoOutput::drawSource (juce::Graphics& g, int d, float alpha)
{
    if (alpha <= 0.001f)
        return;
    const auto area = getLocalBounds().toFloat();
    const auto frame = videos[(size_t) d]->getFrame();
    g.setOpacity (alpha);
    if (frame.isValid())
    {
        // Sin deformar: bandas negras arriba y abajo (o a los lados) si la proporcion no coincide.
        const auto placed = juce::RectanglePlacement (juce::RectanglePlacement::centred)
                                .appliedTo (frame.getBounds().toFloat(), area);
        g.drawImage (frame, placed, juce::RectanglePlacement::stretchToFit);
        return;
    }

    // Cancion sin video: rotulo con el titulo (VirtualDJ muestra la caratula o el titulo).
    const auto title = shownTitles[(size_t) d];
    if (title.isEmpty())
        return;
    const auto accent = d == 0 ? juce::Colour (0xff29b6f6) : juce::Colour (0xffffa726);
    g.setGradientFill (juce::ColourGradient (accent.darker (2.2f).withAlpha (alpha), area.getTopLeft(),
                                             juce::Colour (0xff050608).withAlpha (alpha), area.getBottomRight(), false));
    g.fillRect (area);
    const float h = area.getHeight();
    auto text = area.reduced (area.getWidth() * 0.06f, h * 0.1f);
    const auto artist = title.upToFirstOccurrenceOf (" - ", false, false);
    const auto name = title.contains (" - ") ? title.fromFirstOccurrenceOf (" - ", false, false) : title;
    g.setColour (juce::Colours::white.withAlpha (alpha));
    g.setFont (juce::FontOptions (h * 0.11f, juce::Font::bold));
    g.drawFittedText (name, text.removeFromTop (h * 0.5f).toNearestInt(), juce::Justification::centredBottom, 2, 0.8f);
    if (title.contains (" - "))
    {
        g.setColour (accent.brighter (0.3f).withAlpha (alpha));
        g.setFont (juce::FontOptions (h * 0.065f));
        g.drawFittedText (artist, text.removeFromTop (h * 0.12f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }
    if (const double bpm = decks[(size_t) d]->getCurrentBpm(); bpm > 0.0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.45f * alpha));
        g.setFont (juce::FontOptions (h * 0.045f));
        g.drawFittedText (juce::String (bpm, 1) + " BPM", text.removeFromTop (h * 0.1f).toNearestInt(), juce::Justification::centred, 1);
    }
}

void VideoOutput::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    const bool a = videos[0]->hasVideo() || shownTitles[0].isNotEmpty();
    const bool b = videos[1]->hasVideo() || shownTitles[1].isNotEmpty();
    const auto w = weights (mixer.getCrossfader(), a, b);
    if (! a && ! b)
    {
        g.setColour (juce::Colours::white.withAlpha (0.2f));
        g.setFont (juce::FontOptions (getHeight() * 0.08f, juce::Font::bold));
        g.drawText ("MiDJ", getLocalBounds(), juce::Justification::centred);
        return;
    }
    // Mezcla: el deck A debajo y el B encima con su parte del total (fundido cruzado).
    if (w[0] > 0.0f && w[1] > 0.0f)
    {
        drawSource (g, 0, 1.0f);
        drawSource (g, 1, w[1] / (w[0] + w[1]));
    }
    else
    {
        drawSource (g, w[0] > 0.0f ? 0 : 1, 1.0f);
    }
}

void VideoOutput::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDoubleClick)
        onDoubleClick();
}

// --- Ventana ---------------------------------------------------------------------------------

VideoWindow::VideoWindow (std::array<DeckVideo*, 2> videos, std::array<const Deck*, 2> decks, const Mixer& mixer)
    : juce::DocumentWindow ("MiDJ - Video", juce::Colours::black, juce::DocumentWindow::allButtons)
{
    output = new VideoOutput (videos, decks, mixer);
    output->onDoubleClick = [this] { toggleFullScreen(); };
    output->setSize (640, 360);
    setContentOwned (output, true);
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    setResizeLimits (320, 180, 8192, 8192);
    setWantsKeyboardFocus (true);
}

void VideoWindow::toggleFullScreen()
{
    // A mano y no con setFullScreen: con la barra de titulo de Windows eso solo maximiza, y en la
    // pantalla del publico no debe verse ni la barra ni el borde.
    if (fullScreenMode)
    {
        fullScreenMode = false;
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        if (! windowedBounds.isEmpty())
            setBounds (windowedBounds);
        return;
    }
    windowedBounds = getBounds();
    // Con dos monitores, al que no es el principal (el del publico); si no, al de la ventana.
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    auto area = displays.getDisplayForRect (getScreenBounds()) != nullptr
                    ? displays.getDisplayForRect (getScreenBounds())->totalArea
                    : displays.getPrimaryDisplay()->totalArea;
    for (const auto& display : displays.displays)
        if (! display.isMain)
        {
            area = display.totalArea;
            break;
        }
    fullScreenMode = true;
    setUsingNativeTitleBar (false);
    setTitleBarHeight (0);
    setResizable (false, false);
    setBounds (area);
    toFront (true);
}

void VideoWindow::closeButtonPressed()
{
    if (fullScreenMode)
        toggleFullScreen();
    setVisible (false);
    if (onClosed)
        onClosed();
}

bool VideoWindow::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && fullScreenMode)
    {
        toggleFullScreen();
        return true;
    }
    return juce::DocumentWindow::keyPressed (key);
}
}
