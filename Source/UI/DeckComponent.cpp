#include "UI/DeckComponent.h"
#include "UI/DarkLookAndFeel.h"
#include "UI/LibraryBrowser.h"

#include <cmath>

namespace midj
{
namespace
{
    constexpr double tempoRangePercent = 16.0;
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
    playButton.onClick = [this]
    {
        deck.isPlaying() ? deck.pause() : deck.play();
        updateLabels();
    };
    syncButton.onClick = [this]
    {
        if (! deck.syncTo (otherDeck))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "MiDJ",
                                                    "SYNC necesita una cancion con BPM en los dos decks.");
    };
    keyButton.onClick = [this]
    {
        deck.setKeyLock (! deck.isKeyLocked());
        updateLabels();
    };
    keyButton.setTooltip ("Keylock: cambia el tempo sin cambiar el tono");

    // Stems: STEMS pide la separacion; cuando esta lista, VOZ e INSTR encienden / apagan cada parte.
    stemsButton.setTooltip ("Separar voz e instrumental con IA (en segundo plano; en este equipo tarda algo mas que la cancion)");
    stemsButton.onClick = [this] { if (onStemsRequested) onStemsRequested(); };
    vocalButton.setTooltip ("Voz encendida / apagada (apagada = instrumental)");
    vocalButton.onClick = [this] { deck.setVocalsOn (! deck.isVocalsOn()); updateLabels(); };
    instrumentalButton.setTooltip ("Instrumental encendido / apagado (apagado = a capela)");
    instrumentalButton.onClick = [this] { deck.setInstrumentalOn (! deck.isInstrumentalOn()); updateLabels(); };
    for (auto* b : { &stemsButton, &vocalButton, &instrumentalButton })
        addChildComponent (b);
    for (auto* b : { &loadButton, &cueButton, &playButton, &syncButton, &keyButton })
        addAndMakeVisible (b);
    cueButton.setColour (juce::TextButton::textColourOffId, cueColour);

    // Bucles: pulsar un tamano activa ese bucle; pulsar el activo lo quita.
    for (size_t i = 0; i < loopButtons.size(); ++i)
    {
        auto& b = loopButtons[i];
        b.setButtonText (juce::String (loopSizes[i]));
        b.setClickingTogglesState (false);
        b.onClick = [this, beats = loopSizes[i]]
        {
            if (deck.getLoopBeats() == beats) deck.exitLoop();
            else                             deck.setLoop (beats);
        };
        addAndMakeVisible (b);
    }
    loopLabel.setText ("LOOP", juce::dontSendNotification);
    loopLabel.setJustificationType (juce::Justification::centredRight);
    loopLabel.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
    addAndMakeVisible (loopLabel);

    // Tempo +-16 %, como el fader de pitch de un plato con rango ampliado.
    tempoSlider.setSliderStyle (juce::Slider::LinearVertical);
    tempoSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    tempoSlider.setRange (-tempoRangePercent, tempoRangePercent, 0.01);
    tempoSlider.setValue (0.0, juce::dontSendNotification);
    tempoSlider.setDoubleClickReturnValue (true, 0.0);
    tempoSlider.onValueChange = [this]
    {
        deck.setSpeed (1.0 + tempoSlider.getValue() / 100.0);
        updateLabels();
    };
    addAndMakeVisible (tempoSlider);

    for (auto* label : { &titleLabel, &bpmLabel, &timeLabel, &tempoLabel })
        addAndMakeVisible (label);
    titleLabel.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    bpmLabel.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    bpmLabel.setJustificationType (juce::Justification::centredRight);
    timeLabel.setJustificationType (juce::Justification::centredRight);
    timeLabel.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
    tempoLabel.setJustificationType (juce::Justification::centred);
    tempoLabel.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);

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

void DeckComponent::updateLabels()
{
    const bool hasTrack = deck.hasTrack();
    titleLabel.setText (deckName + "   " + (deck.isLoading() ? juce::String ("Cargando...")
                                           : hasTrack ? deck.getTrackName() : juce::String ("Sin cancion")),
                        juce::dontSendNotification);

    const double bpm = deck.getCurrentBpm();
    bpmLabel.setText (bpm > 0.0 ? juce::String (bpm, 2) + " BPM" : juce::String ("--- BPM"), juce::dontSendNotification);

    timeLabel.setText (formatTime (deck.getPositionSeconds()) + " / " + formatTime (deck.getLengthSeconds()),
                       juce::dontSendNotification);

    // El fader sigue a la velocidad real (SYNC la cambia), salvo mientras se arrastra.
    const double percent = (deck.getSpeed() - 1.0) * 100.0;
    if (! tempoSlider.isMouseButtonDown())
        tempoSlider.setValue (percent, juce::dontSendNotification);
    tempoLabel.setText ((percent >= 0.0 ? "+" : "") + juce::String (percent, 2) + " %", juce::dontSendNotification);

    playButton.setButtonText (deck.isPlaying() ? "Pausa" : "Play");

    // Stems: botones VOZ / INSTR si ya estan; si no, STEMS con el estado de la separacion.
    const bool stems = deck.hasStems();
    vocalButton.setVisible (stems);
    instrumentalButton.setVisible (stems);
    stemsButton.setVisible (! stems && hasTrack);
    for (auto [button, on] : { std::pair<juce::TextButton*, bool> { &vocalButton, deck.isVocalsOn() },
                               std::pair<juce::TextButton*, bool> { &instrumentalButton, deck.isInstrumentalOn() } })
        button->setColour (juce::TextButton::buttonColourId, on ? accent().withAlpha (0.6f) : DarkLookAndFeel::panel.brighter (0.15f));
    if (! stems && getStemsState)
    {
        const auto [state, progress] = getStemsState();
        stemsButton.setEnabled (state == StemsState::none || state == StemsState::failed);
        stemsButton.setButtonText (state == StemsState::working ? "STEMS " + juce::String (juce::roundToInt (progress * 100.0)) + " %"
                                   : state == StemsState::queued ? juce::String ("STEMS...")
                                   : state == StemsState::failed ? juce::String ("STEMS (error)") : juce::String ("STEMS"));
    }
    keyButton.setColour (juce::TextButton::buttonColourId,
                         deck.isKeyLocked() ? accent().withAlpha (0.6f) : DarkLookAndFeel::panel.brighter (0.15f));
    for (size_t i = 0; i < loopButtons.size(); ++i)
        loopButtons[i].setColour (juce::TextButton::buttonColourId,
                                  deck.getLoopBeats() == loopSizes[i] ? accent().withAlpha (0.6f)
                                                                      : DarkLookAndFeel::panel.brighter (0.15f));
}

void DeckComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateLabels();
    repaint();
}

void DeckComponent::timerCallback()
{
    updateLabels();
    repaint (waveformArea);
}

float DeckComponent::timeToX (double seconds) const
{
    const double length = deck.getLengthSeconds();
    if (length <= 0.0)
        return (float) waveformArea.getX();
    return (float) waveformArea.getX() + (float) (seconds / length) * (float) waveformArea.getWidth();
}

// Rejilla de pulsos: linea tenue en cada pulso y mas marcada cada 4 (inicio de compas).
// Si los pulsos quedarian a menos de 4 px, solo se dibujan los inicios de compas.
void DeckComponent::paintBeatGrid (juce::Graphics& g)
{
    const double bpm = deck.getOriginalBpm(), length = deck.getLengthSeconds();
    if (bpm <= 0.0 || length <= 0.0)
        return;
    const double period = 60.0 / bpm;
    const double pixelsPerBeat = period / length * waveformArea.getWidth();
    const int step = pixelsPerBeat < 4.0 ? 4 : 1;
    const auto top = (float) waveformArea.getY(), bottom = (float) waveformArea.getBottom();
    for (int beat = 0; ; beat += step)
    {
        const double t = deck.beatTime (beat);
        if (t > length)
            break;
        const bool barStart = beat % 4 == 0;
        g.setColour (DarkLookAndFeel::text.withAlpha (barStart ? 0.35f : 0.12f));
        g.drawVerticalLine ((int) timeToX (t), top, bottom);
    }
}

void DeckComponent::paint (juce::Graphics& g)
{
    g.setColour (DarkLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    if (active && ! dragHighlight)   // el deck activo (el de los atajos de teclado): marco fino
    {
        g.setColour (accent().withAlpha (0.7f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 8.0f, 1.5f);
    }
    if (dragHighlight)   // se esta arrastrando una cancion encima: marco del color del deck
    {
        g.setColour (accent());
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 8.0f, 3.0f);
    }

    g.setColour (DarkLookAndFeel::background);
    g.fillRect (waveformArea);
    if (! deck.hasTrack())
        return;

    const auto top = (float) waveformArea.getY(), bottom = (float) waveformArea.getBottom();

    // Zona del bucle, detras de la forma de onda.
    if (deck.isLooping())
    {
        const float x0 = timeToX (deck.getLoopStart()), x1 = timeToX (deck.getLoopEnd());
        g.setColour (accent().withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (x0, top, juce::jmax (2.0f, x1 - x0), bottom - top));
    }

    paintBeatGrid (g);

    auto& thumb = deck.getThumbnail();
    if (thumb.getTotalLength() > 0.0)
    {
        g.setColour (accent().withAlpha (0.85f));
        thumb.drawChannel (g, waveformArea.reduced (2), 0.0, thumb.getTotalLength(), 0, 1.0f);   // una sola onda (no una por canal)
    }

    // Punto CUE: linea y triangulo naranjas arriba.
    const float cx = timeToX (deck.getCuePoint());
    g.setColour (cueColour);
    g.drawVerticalLine ((int) cx, top, bottom);
    juce::Path tri;
    tri.addTriangle (cx - 6.0f, top, cx + 6.0f, top, cx, top + 9.0f);
    g.fillPath (tri);

    // Cabezal de reproduccion.
    const float px = timeToX (deck.getPositionSeconds());
    g.setColour (DarkLookAndFeel::text);
    g.drawLine (px, top, px, bottom, 2.0f);
}

void DeckComponent::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto header = area.removeFromTop (36);
    bpmLabel.setBounds (header.removeFromRight (200));
    titleLabel.setBounds (header);
    timeLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (4);

    auto right = area.removeFromRight (70);
    tempoLabel.setBounds (right.removeFromBottom (20));
    keyButton.setBounds (right.removeFromBottom (30).reduced (6, 2));
    right.removeFromBottom (4);
    tempoSlider.setBounds (right);

    // Fila inferior: CUE, Play, SYNC y Cargar a la derecha.
    auto transport = area.removeFromBottom (44);
    const int bw = 86;
    cueButton.setBounds (transport.removeFromLeft (bw).reduced (0, 4));
    transport.removeFromLeft (6);
    playButton.setBounds (transport.removeFromLeft (bw).reduced (0, 4));
    transport.removeFromLeft (6);
    syncButton.setBounds (transport.removeFromLeft (bw).reduced (0, 4));
    loadButton.setBounds (transport.removeFromRight (bw).reduced (0, 4).withTrimmedRight (8));

    // Fila de bucles y, encima, la de efectos.
    area.removeFromBottom (4);
    auto loops = area.removeFromBottom (30);
    loopLabel.setBounds (loops.removeFromLeft (44));
    loops.removeFromLeft (6);
    for (auto& b : loopButtons)
    {
        b.setBounds (loops.removeFromLeft (48).reduced (0, 2));
        loops.removeFromLeft (6);
    }
    // A la derecha de los bucles: los stems.
    auto stemsArea = loops.removeFromRight (juce::jmin (170, loops.getWidth())).withTrimmedRight (8);
    stemsButton.setBounds (stemsArea.reduced (0, 2));
    vocalButton.setBounds (stemsArea.removeFromLeft (stemsArea.getWidth() / 2).reduced (0, 2).withTrimmedRight (3));
    instrumentalButton.setBounds (stemsArea.reduced (0, 2).withTrimmedLeft (3));
    area.removeFromBottom (2);
    effectsRow.setBounds (area.removeFromBottom (36));

    area.removeFromBottom (8);
    waveformArea = area.reduced (0, 4).withTrimmedRight (8);
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
