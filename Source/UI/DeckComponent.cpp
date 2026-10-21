#include "UI/DeckComponent.h"
#include "UI/DarkLookAndFeel.h"
#include "UI/LibraryBrowser.h"
#include "Video/DeckVideo.h"

#include <cmath>

namespace midj
{
namespace
{
    const juce::Colour cueColour { 0xffffa726 };   // naranja, como en las mesas de DJ

    juce::String formatTime (double seconds)
    {
        const int total = (int) std::max (0.0, seconds);
        return juce::String::formatted ("%d:%02d", total / 60, total % 60);
    }
}

DeckComponent::DeckComponent (Deck& d, Deck& other, EffectRack& effects, const juce::String& name)
    : deck (d), otherDeck (other), deckName (name),
      effectsRow (effects, name == "A" ? DarkLookAndFeel::accentA : DarkLookAndFeel::accentB)
{
    addAndMakeVisible (effectsRow);
    addMouseListener (this, true);   // los clics en botones y mandos tambien hacen activo el deck
    deck.addChangeListener (this);
    deck.getThumbnail().addChangeListener (this);

    loadButton.onClick = [this] { chooseFile(); };
    cueButton.onClick = [this] { deck.cue(); };
    cueButton.setTooltip ("CUE: vuelve al punto de inicio (tecla C en el deck activo)");
    playButton.onClick = [this]
    {
        deck.isPlaying() ? deck.pause() : deck.play();
        updateLabels();
    };
    playButton.setTooltip ("Play / Pausa (Espacio en el deck activo)");
    syncButton.onClick = [this]
    {
        if (! deck.syncTo (otherDeck))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "MiDJ",
                                                    "SYNC necesita una cancion con BPM en los dos decks.");
    };
    syncButton.setTooltip ("Igualar el tempo y el pulso con el otro deck (tecla S)");
    keyButton.onClick = [this]
    {
        deck.setKeyLock (! deck.isKeyLocked());
        updateLabels();
    };
    keyButton.setTooltip ("Keylock: cambia el tempo sin cambiar el tono (tecla K)");

    // Stems: "Separar stems" pide la separacion; cuando esta lista, Voz e Instrumental encienden / apagan cada parte.
    stemsButton.setTooltip ("Separar voz e instrumental con IA (en segundo plano; en este equipo tarda algo mas que la cancion)");
    stemsButton.onClick = [this] { if (onStemsRequested) onStemsRequested(); };
    vocalButton.setTooltip ("Voz encendida / apagada (apagada = instrumental)");
    vocalButton.onClick = [this] { deck.setVocalsOn (! deck.isVocalsOn()); updateLabels(); };
    instrumentalButton.setTooltip ("Instrumental encendido / apagado (apagado = a capela)");
    instrumentalButton.onClick = [this] { deck.setInstrumentalOn (! deck.isInstrumentalOn()); updateLabels(); };
    for (auto* b : { &stemsButton, &vocalButton, &instrumentalButton })
        addChildComponent (b);
    for (auto* b : { &cueButton, &playButton, &syncButton, &keyButton })
        addAndMakeVisible (b);
    addAndMakeVisible (loadButton);
    playButton.getProperties().set (DarkLookAndFeel::filledProperty, true);

    // Bucles: pulsar un tamano activa ese bucle; pulsar el activo lo quita.
    for (size_t i = 0; i < loopButtons.size(); ++i)
    {
        auto& b = loopButtons[i];
        b.setButtonText (juce::String (loopSizes[i]));
        b.setTooltip ("Bucle de " + juce::String (loopSizes[i]) + (loopSizes[i] == 1 ? " pulso" : " pulsos") + " (otra vez: salir)");
        b.onClick = [this, beats = loopSizes[i]]
        {
            if (deck.getLoopBeats() == beats) deck.exitLoop();
            else                             deck.setLoop (beats);
        };
        addAndMakeVisible (b);
    }

    // Pitch +-16 %, con el centro marcado y enganche cerca del 0.
    tempoSlider.setThumbColour (accent());
    tempoSlider.setFillColour (accent().withAlpha (0.7f));
    tempoSlider.setTooltip ("Pitch (tempo) +-16 %. Doble clic: 0 %");
    tempoSlider.onValueChange = [this]
    {
        deck.setSpeed (1.0 + tempoSlider.getValue() / 100.0);
        updateLabels();
    };
    addAndMakeVisible (tempoSlider);

    updateLabels();
    startTimerHz (30);   // refresco de posicion y forma de onda
}

DeckComponent::~DeckComponent()
{
    deck.getThumbnail().removeChangeListener (this);
    deck.removeChangeListener (this);
}

juce::Colour DeckComponent::accent() const
{
    return deckName == "A" ? DarkLookAndFeel::accentA : DarkLookAndFeel::accentB;
}

void DeckComponent::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Elige una cancion",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.mp3;*.wav;*.flac;*.ogg;*.aif;*.aiff;*.mp4;*.m4a;*.mov;*.m4v;*.wmv;*.wma;*.aac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file.existsAsFile())
                                  loadFile (file);
                          });
}

void DeckComponent::addLearnTargets (std::vector<LearnTarget>& out, const juce::String& prefix)
{
    out.push_back ({ &playButton, prefix + "play" });
    out.push_back ({ &cueButton, prefix + "cue" });
    out.push_back ({ &syncButton, prefix + "sync" });
    out.push_back ({ &keyButton, prefix + "keylock" });
    out.push_back ({ &loadButton, prefix + "load" });
    for (size_t i = 0; i < loopButtons.size(); ++i)
        out.push_back ({ &loopButtons[i], prefix + "loop" + juce::String (loopSizes[i]) });
    out.push_back ({ &tempoSlider, prefix + "tempo" });
    out.push_back ({ &vocalButton, prefix + "vocals" });
    out.push_back ({ &instrumentalButton, prefix + "instrumental" });
    if (sections.waveform)
        out.push_back ({ this, prefix + "jog", waveformArea });   // la forma de onda hace de plato
    effectsRow.addLearnTargets (out, prefix);
}

void DeckComponent::loadFile (const juce::File& file)
{
    if (onLoadFile)
        onLoadFile (file);
    else if (! deck.load (file))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MiDJ", "No se pudo abrir el archivo.");
}

void DeckComponent::setPoster (const juce::File& forFile, const juce::Image& image)
{
    posterFile = forFile;
    poster = image;
    repaint (posterArea);
}

bool DeckComponent::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.toString().startsWith (LibraryBrowser::dragPrefix);
}

void DeckComponent::itemDropped (const SourceDetails& details)
{
    setDragHighlight (false);
    loadFile (juce::File (details.description.toString().fromFirstOccurrenceOf (LibraryBrowser::dragPrefix, false, false)));
}

bool DeckComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && Library::isAudioFile (juce::File (files[0]));
}

void DeckComponent::filesDropped (const juce::StringArray& files, int, int)
{
    setDragHighlight (false);
    if (! files.isEmpty())
        loadFile (juce::File (files[0]));
}

void DeckComponent::styleToggle (juce::TextButton& b, bool on, bool filled)
{
    b.getProperties().set (DarkLookAndFeel::filledProperty, on && filled);
    b.setColour (juce::TextButton::buttonColourId, on ? (filled ? accent() : accent().withAlpha (0.18f)) : juce::Colours::transparentBlack);
    b.setColour (juce::TextButton::textColourOffId, on ? (filled ? DarkLookAndFeel::background : DarkLookAndFeel::text) : DarkLookAndFeel::textQuiet);
    b.setColour (juce::ComboBox::outlineColourId, on ? accent() : DarkLookAndFeel::line);
}

void DeckComponent::updateLabels()
{
    const bool hasTrack = deck.hasTrack();
    const auto file = deck.getLoadedFile();

    // Titulo y artista: se piden una vez por archivo (la biblioteca va a disco).
    if (file != describedFile || deck.isLoading())
    {
        describedFile = deck.isLoading() ? juce::File() : file;
        title = deck.getTrackName();
        artist = {};
        if (describe && hasTrack)
        {
            const auto [t, a] = describe (file);
            if (t.isNotEmpty())
            {
                title = t;
                artist = a;
            }
        }
    }
    juce::String newState = deck.isLoading() ? "cargando..." : ! hasTrack ? "vacio" : deck.isPlaying() ? "sonando" : "en pausa";
    const double bpm = deck.getCurrentBpm();
    juce::String newBpm = hasTrack && bpm > 0.0 ? juce::String (bpm, 1) : juce::String ("---");
    juce::String newTime = formatTime (deck.getPositionSeconds()) + " / " + formatTime (deck.getLengthSeconds());

    // El fader sigue a la velocidad real (SYNC la cambia), salvo mientras se arrastra.
    const double percent = (deck.getSpeed() - 1.0) * 100.0;
    if (! tempoSlider.isMouseButtonDown())
        tempoSlider.setValue (std::abs (percent) < 0.005 ? 0.0 : percent, juce::dontSendNotification);
    const double shown = tempoSlider.getValue();
    juce::String newPitch = (shown >= 0.0 ? "+" : "") + juce::String (shown, 2) + " %";

    if (newState != stateText || newBpm != bpmText || newTime != timeText)
    {
        stateText = newState;
        bpmText = newBpm;
        timeText = newTime;
        repaint (headerArea);
        repaint (infoArea);
    }
    if (newPitch != pitchText)
    {
        pitchText = newPitch;
        repaint (pitchLabelArea);
        repaint (pitchScaleArea);
    }

    const bool playing = deck.isPlaying();
    playButton.setButtonText (playing ? "Pausa" : "Play");
    playButton.getProperties().set (DarkLookAndFeel::filledProperty, playing);
    playButton.setColour (juce::TextButton::buttonColourId, playing ? accent() : juce::Colours::transparentBlack);
    playButton.setColour (juce::TextButton::textColourOffId, playing ? DarkLookAndFeel::background : accent());
    playButton.setColour (juce::ComboBox::outlineColourId, accent());

    // Stems: Voz / Instrumental si ya estan; si no, "Separar stems" con el estado de la separacion.
    const bool stems = deck.hasStems();
    vocalButton.setVisible (sections.stems && stems);
    instrumentalButton.setVisible (sections.stems && stems);
    stemsButton.setVisible (sections.stems && ! stems);
    styleToggle (vocalButton, deck.isVocalsOn(), false);
    styleToggle (instrumentalButton, deck.isInstrumentalOn(), false);
    if (! stems)
    {
        auto [state, progress] = getStemsState ? getStemsState() : std::pair<StemsState, double> { StemsState::none, 0.0 };
        stemsButton.setEnabled (hasTrack && (state == StemsState::none || state == StemsState::failed));
        stemsButton.setButtonText (state == StemsState::working ? "Separando " + juce::String (juce::roundToInt (progress * 100.0)) + " %"
                                   : state == StemsState::queued ? juce::String ("En cola...")
                                   : state == StemsState::failed ? juce::String ("Error: reintentar") : juce::String ("Separar stems"));
    }
    styleToggle (keyButton, deck.isKeyLocked());
    for (size_t i = 0; i < loopButtons.size(); ++i)
        styleToggle (loopButtons[i], deck.getLoopBeats() == loopSizes[i]);
}

void DeckComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    waveDirty = true;   // cancion nueva, onda cargando o BPM analizado: rehacer la imagen de la onda
    updateLabels();
    repaint();
}

void DeckComponent::timerCallback()
{
    updateLabels();
    repaint (waveformArea);
    if (titleScrolls)
        repaint (titleArea);
}

void DeckComponent::paintTitle (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area)
{
    // Cabe (o casi: un poco estrecho): quieto. Si no, se desplaza a 30 px/s con 2 s de pausa al
    // principio de cada vuelta, para leerlo entero.
    titleArea = area;
    const float width = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
    const bool scrolls = width * 0.85f > (float) area.getWidth();
    if (scrolls != titleScrolls || text != scrolledTitle)   // otra cancion: desde el principio
    {
        titleScrolls = scrolls;
        scrolledTitle = text;
        titleScrollStart = juce::Time::getMillisecondCounter();
    }
    if (! scrolls)
    {
        g.drawFittedText (text, area, juce::Justification::centredLeft, 1, 0.85f);
        return;
    }
    constexpr float gap = 40.0f, speed = 0.03f, pauseMs = 2000.0f;
    const float loop = width + gap;
    const float t = (float) ((juce::Time::getMillisecondCounter() - titleScrollStart) % (juce::uint32) (pauseMs + loop / speed));
    const float offset = juce::jmax (0.0f, t - pauseMs) * speed;
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (area);
    for (float x = (float) area.getX() - offset; x < (float) area.getRight(); x += loop)
        g.drawText (text, juce::Rectangle<float> (x, (float) area.getY(), width + 2.0f, (float) area.getHeight()), juce::Justification::centredLeft, false);
}

float DeckComponent::timeToX (double seconds) const
{
    const double length = deck.getLengthSeconds();
    if (length <= 0.0)
        return (float) waveformArea.getX();
    return (float) waveformArea.getX() + (float) (seconds / length) * (float) waveformArea.getWidth();
}

void DeckComponent::renderWaveCache (float scale)
{
    // La onda (con la rejilla de pulsos) se dibuja una sola vez en dos imagenes: apagada (lo ya
    // sonado) y del color del deck (lo que falta). En cada fotograma solo se copian.
    const int w = juce::roundToInt ((float) waveformArea.getWidth() * scale), h = juce::roundToInt ((float) waveformArea.getHeight() * scale);
    waveCacheSize = waveformArea.getWidth() * 10000 + waveformArea.getHeight();
    waveCacheScale = scale;
    waveDirty = false;
    waveBright = waveDim = {};
    if (w <= 0 || h <= 0 || ! deck.hasTrack())
        return;
    auto& thumb = deck.getThumbnail();
    const double length = deck.getLengthSeconds();
    for (auto* target : { &waveDim, &waveBright })
    {
        *target = juce::Image (juce::Image::RGB, w, h, true);
        juce::Graphics g (*target);
        g.addTransform (juce::AffineTransform::scale (scale));
        const auto area = juce::Rectangle<int> (0, 0, waveformArea.getWidth(), waveformArea.getHeight());
        g.fillAll (DarkLookAndFeel::background);
        // Rejilla de pulsos: tenue en cada pulso, mas marcada cada 4 (si quedan a menos de 4 px, solo los compases).
        const double bpm = deck.getOriginalBpm();
        if (bpm > 0.0 && length > 0.0)
        {
            const double pixelsPerBeat = 60.0 / bpm / length * area.getWidth();
            const int step = pixelsPerBeat < 4.0 ? 4 : 1;
            for (int beat = 0; ; beat += step)
            {
                const double t = deck.beatTime (beat);
                if (t > length)
                    break;
                g.setColour (DarkLookAndFeel::text.withAlpha (beat % 4 == 0 ? 0.22f : 0.08f));
                g.drawVerticalLine ((int) (t / length * area.getWidth()), 0.0f, (float) area.getHeight());
            }
        }
        if (thumb.getTotalLength() > 0.0)
        {
            g.setColour (target == &waveDim ? accent().withAlpha (0.35f) : accent());
            thumb.drawChannel (g, area.reduced (2, 3), 0.0, thumb.getTotalLength(), 0, 1.0f);   // una sola onda (no una por canal)
        }
    }
}

void DeckComponent::paintWaveform (juce::Graphics& g)
{
    g.setColour (DarkLookAndFeel::background);
    g.fillRect (waveformArea);
    if (! deck.hasTrack())
        return;
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (waveDirty || waveCacheScale != scale || waveCacheSize != waveformArea.getWidth() * 10000 + waveformArea.getHeight())
        renderWaveCache (scale);
    const auto top = (float) waveformArea.getY(), bottom = (float) waveformArea.getBottom();
    const float px = timeToX (deck.getPositionSeconds());

    // Lo ya sonado de la imagen apagada y lo que falta de la del color del deck.
    if (waveBright.isValid())
    {
        const auto toArea = juce::AffineTransform::scale (1.0f / waveCacheScale).translated ((float) waveformArea.getX(), top);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (waveformArea.withRight ((int) px));
            g.drawImageTransformed (waveDim, toArea);
        }
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (waveformArea.withLeft ((int) px));
        g.drawImageTransformed (waveBright, toArea);
    }

    // Zona del bucle encima, transparente.
    if (deck.isLooping())
    {
        const float x0 = timeToX (deck.getLoopStart()), x1 = timeToX (deck.getLoopEnd());
        g.setColour (accent().withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (x0, top, juce::jmax (2.0f, x1 - x0), bottom - top));
    }

    // Punto CUE: linea y triangulo naranjas arriba.
    const float cx = timeToX (deck.getCuePoint());
    g.setColour (cueColour);
    g.drawVerticalLine ((int) cx, top, bottom);
    juce::Path tri;
    tri.addTriangle (cx - 5.0f, top, cx + 5.0f, top, cx, top + 7.0f);
    g.fillPath (tri);

    // Cabezal de reproduccion.
    g.setColour (DarkLookAndFeel::text);
    g.fillRect (juce::Rectangle<float> (px - 1.0f, top - 2.0f, 2.0f, bottom - top + 4.0f));
}

void DeckComponent::paintPoster (juce::Graphics& g)
{
    const auto r = posterArea.toFloat();
    juce::Path shape;
    shape.addRoundedRectangle (r, 8.0f);
    const bool video = deck.hasTrack() && DeckVideo::isVideoFile (deck.getLoadedFile());
    const bool hasPoster = poster.isValid() && posterFile == deck.getLoadedFile();
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (shape);
        g.setColour (video ? accent().withAlpha (0.12f).overlaidWith (DarkLookAndFeel::raised.withAlpha (0.6f)) : DarkLookAndFeel::raised);
        g.fillRect (r);
        if (hasPoster)
            g.drawImage (poster, r, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
        else
        {
            g.setColour (DarkLookAndFeel::textQuiet);
            g.setFont (DarkLookAndFeel::sans (12.0f));
            g.drawFittedText (! deck.hasTrack() ? juce::String ("Carga una cancion") : video ? juce::String ("Video") : juce::String ("Solo audio"),
                              posterArea.reduced (6), juce::Justification::centred, 2);
        }
    }
    if (deck.hasTrack())
    {
        // Etiqueta abajo a la izquierda: video o audio.
        const auto badge = juce::Rectangle<float> (r.getX() + 6.0f, r.getBottom() - 24.0f, video ? 50.0f : 48.0f, 18.0f);
        g.setColour (DarkLookAndFeel::background.withAlpha (0.85f));
        g.fillRoundedRectangle (badge, 4.0f);
        Icons::draw (g, badge.withWidth (18.0f).reduced (3.0f), video ? Icons::Glyph::film : Icons::Glyph::note, DarkLookAndFeel::text);
        g.setColour (DarkLookAndFeel::text);
        g.setFont (DarkLookAndFeel::sans (11.0f));
        g.drawText (video ? "video" : "audio", badge.withTrimmedLeft (18.0f), juce::Justification::centredLeft, false);
    }
}

void DeckComponent::paint (juce::Graphics& g)
{
    const auto card = getLocalBounds().toFloat();
    g.setColour (DarkLookAndFeel::panel);
    g.fillRoundedRectangle (card, 14.0f);
    if (dragHighlight)   // se esta arrastrando una cancion encima
    {
        g.setColour (accent());
        g.drawRoundedRectangle (card.reduced (1.5f), 14.0f, 3.0f);
    }
    else
    {
        // Deck activo (el de los atajos) o sonando: marco del color del deck.
        g.setColour (active || deck.isPlaying() ? accent().withAlpha (active ? 1.0f : 0.5f) : DarkLookAndFeel::line.withAlpha (0.6f));
        g.drawRoundedRectangle (card.reduced (0.5f), 14.0f, 1.0f);
    }

    // Cabecera: DECK A y su estado.
    auto header = headerArea;
    g.setColour (accent());
    g.setFont (DarkLookAndFeel::sans (12.0f, true).withKerningFactor (0.08f));
    g.drawText ("DECK " + deckName, header, juce::Justification::centredLeft, false);
    g.setColour (DarkLookAndFeel::textQuiet);
    g.setFont (DarkLookAndFeel::mono (12.0f));
    g.drawText (stateText, header.withTrimmedRight (loadButton.getWidth() + 8), juce::Justification::centredRight, false);

    paintPoster (g);

    // Titulo, artista, BPM y tiempo. Con poco alto, el BPM y el tiempo van a la derecha.
    auto info = infoArea;
    const bool compact = info.getHeight() < 74;
    if (compact)
    {
        auto right = info.removeFromRight (84);
        g.setColour (DarkLookAndFeel::text);
        g.setFont (DarkLookAndFeel::mono (22.0f, true));
        g.drawText (bpmText, right.removeFromTop (right.getHeight() / 2 + 6), juce::Justification::bottomRight, false);
        g.setColour (DarkLookAndFeel::textQuiet);
        g.setFont (DarkLookAndFeel::mono (11.0f));
        g.drawText (timeText, right.withTrimmedTop (2), juce::Justification::topRight, false);
        info.removeFromRight (6);
        info = info.withSizeKeepingCentre (info.getWidth(), 40);
    }
    auto bottomRow = compact ? juce::Rectangle<int>() : info.removeFromBottom (34);
    g.setColour (DarkLookAndFeel::text);
    g.setFont (DarkLookAndFeel::sans (compact ? 15.0f : 16.0f, true));
    paintTitle (g, deck.isLoading() ? juce::String ("Cargando...") : deck.hasTrack() ? title : juce::String ("Sin cancion"),
                info.removeFromTop (22));
    g.setColour (DarkLookAndFeel::textQuiet);
    g.setFont (DarkLookAndFeel::sans (13.0f));
    g.drawText (deck.hasTrack() ? artist : juce::String ("Arrastrala o pulsa ") + deckName + " en la biblioteca",
                info.removeFromTop (18), juce::Justification::centredLeft, true);
    if (! compact)
    {
        g.setColour (DarkLookAndFeel::text);
        g.setFont (DarkLookAndFeel::mono (28.0f, true));
        const int bpmWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), bpmText) + 4;
        g.drawText (bpmText, bottomRow.removeFromLeft (bpmWidth), juce::Justification::bottomLeft, false);
        g.setColour (DarkLookAndFeel::textQuiet);
        g.setFont (DarkLookAndFeel::sans (12.0f));
        g.drawText ("BPM", bottomRow.removeFromLeft (34).withTrimmedBottom (4), juce::Justification::bottomLeft, false);
        g.setFont (DarkLookAndFeel::mono (12.0f));
        g.drawText (timeText, bottomRow.withTrimmedBottom (4), juce::Justification::bottomRight, false);
    }

    if (sections.waveform)
        paintWaveform (g);

    // Pitch (las zonas quedan vacias si esta oculto): nombre, valor (verde en 0) y escala con el 0 en el centro del fader.
    const bool centred = tempoSlider.isAtCentre();
    g.setFont (DarkLookAndFeel::sans (12.0f));
    g.setColour (DarkLookAndFeel::textQuiet);
    g.drawText ("Pitch", pitchLabelArea, juce::Justification::centredLeft, false);
    g.setFont (DarkLookAndFeel::mono (12.0f, centred));
    g.setColour (centred ? DarkLookAndFeel::good : DarkLookAndFeel::text);
    g.drawText (pitchText, pitchLabelArea, juce::Justification::centredRight, false);
    const auto scale = pitchScaleArea.withTrimmedLeft (CentredSlider::thumbWidth / 2 - 6).withTrimmedRight (CentredSlider::thumbWidth / 2 - 8);
    g.setFont (DarkLookAndFeel::mono (10.0f));
    g.setColour (DarkLookAndFeel::textFaint);
    g.drawText ("-16", scale, juce::Justification::centredLeft, false);
    g.drawText ("+16", scale, juce::Justification::centredRight, false);
    g.setColour (centred ? DarkLookAndFeel::good : DarkLookAndFeel::textFaint);
    g.drawText ("0", scale, juce::Justification::centred, false);

    g.setFont (DarkLookAndFeel::sans (12.0f));
    g.setColour (DarkLookAndFeel::textQuiet);
    g.drawText ("Loop", loopLabelArea, juce::Justification::centredLeft, false);
}

namespace
{
    // Medidas del deck (alto de cada parte y separacion entre ellas).
    constexpr int padX = 12, padY = 10, headerH = 22, gapH = 8, minInfoH = 60, waveH = 40, pitchH = 50,
                  rowH = 30, effectsH = 38, transportH = 42;
}

void DeckComponent::setSections (const Sections& s)
{
    sections = s;
    tempoSlider.setVisible (s.pitch);
    keyButton.setVisible (s.pitch);
    for (auto& b : loopButtons)
        b.setVisible (s.loops);
    effectsRow.setVisible (s.effects);
    waveDirty = true;
    updateLabels();   // los botones de stems
    resized();
    repaint();
}

int DeckComponent::getPreferredHeight() const
{
    int h = 2 * padY + headerH + gapH + minInfoH + gapH + transportH;
    if (sections.waveform)                 h += gapH + waveH;
    if (sections.pitch)                    h += gapH + pitchH;
    if (sections.loops || sections.stems)  h += gapH + rowH;
    if (sections.effects)                  h += gapH + effectsH;
    return h;
}

void DeckComponent::resized()
{
    auto area = getLocalBounds().reduced (padX, padY);
    headerArea = area.removeFromTop (headerH);
    loadButton.setBounds (headerArea.withLeft (headerArea.getRight() - 28));
    area.removeFromTop (gapH);

    // Transporte abajo del todo; encima, de abajo arriba, las partes que se ven. Lo que sobra
    // queda arriba para la portada y el titulo (asi no hay huecos al ocultar algo).
    auto transport = area.removeFromBottom (transportH);
    const int bw = (transport.getWidth() - 16) / 3;
    playButton.setBounds (transport.removeFromLeft (bw));
    transport.removeFromLeft (8);
    cueButton.setBounds (transport.removeFromLeft (bw));
    transport.removeFromLeft (8);
    syncButton.setBounds (transport);
    area.removeFromBottom (gapH);

    if (sections.effects)
    {
        effectsRow.setBounds (area.removeFromBottom (effectsH));
        area.removeFromBottom (gapH);
    }

    // Bucles y stems en una fila (si solo queda una de las dos, ocupa la fila).
    loopLabelArea = {};
    if (sections.loops || sections.stems)
    {
        auto row = area.removeFromBottom (rowH);
        if (sections.loops)
        {
            loopLabelArea = row.removeFromLeft (36);
            for (auto& b : loopButtons)
            {
                b.setBounds (row.removeFromLeft (34));
                row.removeFromLeft (4);
            }
            row.removeFromLeft (6);
        }
        stemsButton.setBounds (row);
        vocalButton.setBounds (row.removeFromLeft (row.getWidth() * 2 / 5).withTrimmedRight (3));
        instrumentalButton.setBounds (row.withTrimmedLeft (3));
        area.removeFromBottom (gapH);
    }

    // Pitch: nombre y valor, fader (con KEY a la derecha) y escala.
    pitchLabelArea = pitchScaleArea = {};
    if (sections.pitch)
    {
        auto pitch = area.removeFromBottom (pitchH);
        auto key = pitch.removeFromRight (52);
        keyButton.setBounds (key.withSizeKeepingCentre (48, 30).withY (pitch.getY() + 14));
        pitch.removeFromRight (10);
        pitchLabelArea = pitch.removeFromTop (14);
        pitchScaleArea = pitch.removeFromBottom (11);
        tempoSlider.setBounds (pitch);
        area.removeFromBottom (gapH);
    }

    // Forma de onda delgada.
    waveformArea = {};
    if (sections.waveform)
    {
        waveformArea = area.removeFromBottom (waveH);
        area.removeFromBottom (gapH);
    }

    // Portada e informacion con todo lo que queda: lado a lado (portada cuadrada si es poco
    // alto); si sobra mucho, la portada a todo lo ancho y la informacion debajo.
    if (area.getHeight() >= 76 + 10 + 120)
    {
        const int h = juce::jmin (area.getWidth() * 9 / 16, area.getHeight() - 76 - 10);
        posterArea = area.removeFromTop (h).withSizeKeepingCentre (juce::jmin (area.getWidth(), h * 16 / 9), h);
        area.removeFromTop (10);
        infoArea = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), 76));
    }
    else
    {
        auto top = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), 110));
        const int h = top.getHeight();
        posterArea = top.removeFromLeft (h < 74 ? h : juce::jmin (h * 16 / 9, top.getWidth() * 45 / 100));
        top.removeFromLeft (12);
        infoArea = top;
    }
}

void DeckComponent::mouseDown (const juce::MouseEvent& e)
{
    // Un clic en cualquier parte del deck lo hace el deck activo (el de los atajos de teclado).
    if (onActivated)
        onActivated();
    if (e.eventComponent != this)
        return;   // un boton o un mando del deck
    if (waveformArea.contains (e.getPosition()) && deck.getLengthSeconds() > 0.0)
    {
        const double ratio = (double) (e.x - waveformArea.getX()) / (double) waveformArea.getWidth();
        deck.setPositionSeconds (ratio * deck.getLengthSeconds());
    }
}

void DeckComponent::setActive (bool a)
{
    if (active != a)
    {
        active = a;
        repaint();
    }
}
}
