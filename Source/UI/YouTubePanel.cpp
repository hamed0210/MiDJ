#include "UI/YouTubePanel.h"
#include "UI/DarkLookAndFeel.h"

namespace midj
{
namespace
{
    const juce::Colour youTubeRed (0xffff4e45);
}

// --- Lista -----------------------------------------------------------------------------------------

void YouTubeQueue::remove (int index)
{
    if (juce::isPositiveAndBelow (index, (int) items.size()))
    {
        items.erase (items.begin() + index);
        sendChangeMessage();
    }
}

bool YouTubeQueue::takeNext (YouTubeVideo& out)
{
    if (items.empty())
        return false;
    out = items.front();
    items.erase (items.begin());
    sendChangeMessage();
    return true;
}

void YouTubeQueue::save (const juce::File& file) const
{
    juce::XmlElement xml ("YOUTUBE");
    xml.setAttribute ("quality", quality);
    xml.setAttribute ("autoSkipAds", skipAds);   // (el antiguo "skipAds" venia apagado por defecto)
    xml.setAttribute ("signedIn", signedIn);
    for (const auto& v : items)
    {
        auto* e = xml.createNewChildElement ("VIDEO");
        e->setAttribute ("id", v.id);
        e->setAttribute ("title", v.title);
        e->setAttribute ("channel", v.channel);
        e->setAttribute ("seconds", v.seconds);
    }
    xml.writeTo (file);
}

void YouTubeQueue::load (const juce::File& file)
{
    items.clear();
    if (const auto xml = juce::XmlDocument::parse (file))
    {
        quality = xml->getStringAttribute ("quality", "hd720");
        skipAds = xml->getBoolAttribute ("autoSkipAds", true);
        signedIn = xml->getBoolAttribute ("signedIn", false);
        for (auto* e : xml->getChildWithTagNameIterator ("VIDEO"))
            items.push_back ({ e->getStringAttribute ("id"), e->getStringAttribute ("title"), e->getStringAttribute ("channel"),
                               e->getDoubleAttribute ("seconds") });
    }
    sendChangeMessage();
}

// --- Panel -----------------------------------------------------------------------------------------

YouTubePanel::YouTubePanel (YouTubePlayer& p, YouTubeQueue& q) : player (p), queue (q)
{
    list.setModel (this);
    list.setRowHeight (50);
    list.setColour (juce::ListBox::backgroundColourId, DarkLookAndFeel::panel);
    addAndMakeVisible (list);

    playButton.setBordered (true);
    playButton.onClick = [this] { player.togglePlay(); };
    stopButton.setBordered (true);
    stopButton.onClick = [this] { player.close(); };
    nextButton.setBordered (true);
    nextButton.onClick = [this] { next(); };
    clearButton.setBordered (true);
    clearButton.onClick = [this] { queue.clear(); };
    for (auto* b : { &playButton, &stopButton, &nextButton, &clearButton })
        addAndMakeVisible (b);

    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volume.setRange (0.0, 1.0);
    volume.setValue (player.getVolume(), juce::dontSendNotification);
    volume.setTooltip ("Volumen de YouTube (su sonido sale por la tarjeta predeterminada de Windows)");
    volume.onValueChange = [this] { player.setVolume ((float) volume.getValue()); };
    addAndMakeVisible (volume);

    // Calidad y anuncios: lo guardado con la lista.
    player.setQuality (queue.quality);
    player.setSkipAds (queue.skipAds);
    player.setSignedIn (queue.signedIn);
    settingsButton.setTooltip ("Calidad del video de YouTube, saltar anuncios y cuenta de YouTube");
    settingsButton.onClick = [this] { showSettingsMenu(); };
    addAndMakeVisible (settingsButton);

    actionButton.onClick = [this]
    {
        const auto state = player.getStatus().state;
        if (state == YouTubePlayer::State::ad)
            player.skipAd();
        else if (state == YouTubePlayer::State::verify || player.isVerifying())
            player.isVerifying() ? player.hideVerification() : player.showVerification();
    };
    addChildComponent (actionButton);

    player.addChangeListener (this);
    queue.addChangeListener (this);
    thumbnails->addChangeListener (this);
    updateButtons();
}

YouTubePanel::~YouTubePanel()
{
    player.removeChangeListener (this);
    queue.removeChangeListener (this);
    thumbnails->removeChangeListener (this);
    list.setModel (nullptr);
}

void YouTubePanel::playNow (const YouTubeVideo& video)
{
    player.open (video);
}

void YouTubePanel::next()
{
    YouTubeVideo v;
    if (queue.takeNext (v))
        player.open (v);
    else
        player.close();
}

void YouTubePanel::setAutomix (bool on)
{
    queue.setEnabled (on);
    // Al encender sin nada sonando, empieza la lista.
    if (on && ! player.isSounding() && player.getStatus().state != YouTubePlayer::State::paused
        && player.getStatus().state != YouTubePlayer::State::opening && ! queue.getItems().empty())
        next();
}

void YouTubePanel::showSettingsMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Calidad del video");
    for (const auto& q : YouTubePlayer::getQualities())
        menu.addItem (YouTubePlayer::qualityName (q) + (q == "auto" ? " (la que elija YouTube)" : juce::String()), true, q == player.getQuality(),
                      [this, q]
                      {
                          queue.quality = q;
                          player.setQuality (q);
                          updateButtons();
                          resized();
                      });
    menu.addSeparator();
    menu.addItem ("Saltar anuncios", true, player.getSkipAds(), [this]
    {
        queue.skipAds = ! player.getSkipAds();
        player.setSkipAds (queue.skipAds);
        updateButtons();
        resized();
    });
    // Cuenta (Premium: sin anuncios). El reproductor no ve la sesion de Chrome ni de Edge.
    menu.addSectionHeader ("Cuenta de YouTube");
    if (player.isSignedIn())
    {
        menu.addItem ("Sesion iniciada (ver la cuenta)...", [this] { player.showAccount(); });
        menu.addItem ("Cerrar sesion", [this] { player.signOut(); });
    }
    else
    {
        menu.addItem ("Iniciar sesion (con Premium, sin anuncios)...", [this] { player.showAccount(); });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton));
}

juce::String YouTubePanel::getTooltipForRow (int row)
{
    const auto& items = queue.getItems();
    if (! juce::isPositiveAndBelow (row, (int) items.size()))
        return {};
    return items[(size_t) row].title + "\n" + items[(size_t) row].channel;
}

void YouTubePanel::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &thumbnails.get())
    {
        list.repaint();   // llego una miniatura
        repaint (nowArea);
        return;
    }
    if (source == &queue)
    {
        list.updateContent();
        list.repaint();
    }
    queue.signedIn = player.isSignedIn();
    // Un video que no se puede reproducir: con el automix, al siguiente a los 2 s.
    const auto state = player.getStatus().state;
    if (state == YouTubePlayer::State::error && queue.isEnabled() && ! errorPending)
    {
        errorPending = true;
        juce::Timer::callAfterDelay (2000, [safe = juce::Component::SafePointer<YouTubePanel> (this)]
        {
            if (safe != nullptr && safe->player.getStatus().state == YouTubePlayer::State::error)
                safe->next();
            if (safe != nullptr)
                safe->errorPending = false;
        });
    }
    updateButtons();
    repaint (nowArea.getUnion (progressArea).getUnion (timeArea));
}

void YouTubePanel::updateButtons()
{
    const auto state = player.getStatus().state;
    playButton.setGlyph (state == YouTubePlayer::State::playing ? Icons::Glyph::pause : Icons::Glyph::play);
    playButton.setEnabled (state == YouTubePlayer::State::playing || state == YouTubePlayer::State::paused);
    stopButton.setEnabled (player.isOpen());
    nextButton.setEnabled (! queue.getItems().empty());
    clearButton.setEnabled (! queue.getItems().empty());
    settingsButton.setButtonText (YouTubePlayer::qualityName (player.getQuality()) + (player.getSkipAds() ? " | sin anuncios" : ""));

    // Durante un anuncio: saltarlo a mano. Con verificacion: mostrar (u ocultar) la ventana.
    juce::String action;
    if (state == YouTubePlayer::State::ad)
        action = "Saltar anuncio";
    else if (player.isVerifying())
        action = "Ocultar ventana";
    else if (state == YouTubePlayer::State::verify)
        action = "Resolver verificacion";
    actionButton.setButtonText (action);
    actionButton.setTooltip (state == YouTubePlayer::State::ad ? "Saltar el anuncio ahora"
                             : "YouTube pide demostrar que no eres un robot: se abre su ventana para resolverlo "
                               "(captcha o iniciar sesion). Al volver a sonar se esconde sola.");
    actionButton.setColour (juce::TextButton::buttonColourId, state == YouTubePlayer::State::verify && ! player.isVerifying()
                                                                  ? DarkLookAndFeel::danger : DarkLookAndFeel::raised);
    if (actionButton.isVisible() != action.isNotEmpty())
    {
        actionButton.setVisible (action.isNotEmpty());
        resized();
    }
}

void YouTubePanel::paint (juce::Graphics& g)
{
    // Lo que suena: titulo, canal o estado, y la barra de avance con el tiempo.
    const auto& s = player.getStatus();
    auto r = nowArea;
    g.setColour (DarkLookAndFeel::raised);
    g.fillRoundedRectangle (r.toFloat(), 8.0f);
    r.reduce (10, 6);
    const auto& v = player.getVideo();
    // La miniatura del video que suena (sin video, el logo).
    if (player.isOpen() && v.id.isNotEmpty())
    {
        YouTubeThumbnails::draw (g, thumbnails->get (v.id), r.removeFromLeft (60).withSizeKeepingCentre (60, 34).toFloat(), 4.0f);
        r.removeFromLeft (8);
    }
    else
    {
        Icons::drawSource (g, r.removeFromLeft (22).withSizeKeepingCentre (20, 20).toFloat(), SourceType::youtube, DarkLookAndFeel::textFaint);
        r.removeFromLeft (8);
    }
    g.setColour (DarkLookAndFeel::text);
    g.setFont (DarkLookAndFeel::sans (13.0f, true));
    g.drawFittedText (player.isOpen() ? v.title : juce::String ("Nada de YouTube sonando"), r.removeFromTop (18), juce::Justification::centredLeft, 1, 0.75f);
    g.setColour (s.state == YouTubePlayer::State::error || s.state == YouTubePlayer::State::verify ? DarkLookAndFeel::danger
                 : s.state == YouTubePlayer::State::ad ? DarkLookAndFeel::accentB : DarkLookAndFeel::textQuiet);
    g.setFont (DarkLookAndFeel::sans (11.5f));
    const auto state = s.state == YouTubePlayer::State::ad && player.getSkipAds() ? juce::String ("Saltando el anuncio...") : YouTubePlayer::stateText (s);
    const auto line = player.isOpen() ? state + (v.channel.isNotEmpty() ? juce::String::fromUTF8 ("  \xc2\xb7  ") + v.channel : juce::String())
                                      : juce::String ("Busca en la biblioteca > YouTube");
    g.drawText (line, r.removeFromTop (16), juce::Justification::centredLeft, true);

    // Tiempo (a la derecha de los botones) y barra de avance (su propia fila, clic: saltar).
    g.setColour (DarkLookAndFeel::textQuiet);
    g.setFont (DarkLookAndFeel::mono (11.0f));
    const auto timeText = YouTubeSearch::formatDuration (s.time) + (s.duration > 0.0 ? " / " + YouTubeSearch::formatDuration (s.duration) : juce::String());
    g.drawText (player.isOpen() && s.state != YouTubePlayer::State::ad ? timeText : juce::String(), timeArea,
                juce::Justification::centredRight, false);
    auto bar = progressArea.withSizeKeepingCentre (progressArea.getWidth(), 4);
    g.setColour (DarkLookAndFeel::line);
    g.fillRoundedRectangle (bar.toFloat(), 2.0f);
    if (s.duration > 0.0 && s.state != YouTubePlayer::State::ad)
    {
        g.setColour (youTubeRed);
        g.fillRoundedRectangle (bar.toFloat().withWidth ((float) bar.getWidth() * (float) juce::jlimit (0.0, 1.0, s.time / s.duration)), 2.0f);
    }
}

void YouTubePanel::mouseDown (const juce::MouseEvent& e)
{
    // La barra (sin el tiempo de la derecha): saltar a ese punto del video.
    const auto bar = progressArea;
    const auto& s = player.getStatus();
    if (bar.contains (e.getPosition()) && s.duration > 0.0
        && (s.state == YouTubePlayer::State::playing || s.state == YouTubePlayer::State::paused))
        player.seek (s.duration * juce::jlimit (0.0, 1.0, (double) (e.x - bar.getX()) / (double) bar.getWidth()));
}

void YouTubePanel::resized()
{
    auto r = getLocalBounds();
    nowArea = r.removeFromTop (46);
    progressArea = r.removeFromTop (14).reduced (4, 0);
    r.removeFromTop (2);
    auto controls = r.removeFromTop (28);
    playButton.setBounds (controls.removeFromLeft (36));
    controls.removeFromLeft (4);
    stopButton.setBounds (controls.removeFromLeft (36));
    controls.removeFromLeft (8);
    if (actionButton.isVisible())
    {
        // En el sitio del tiempo (durante un anuncio o una verificacion no se ve).
        actionButton.setBounds (controls.removeFromRight (juce::jmin (150, controls.getWidth() / 2)));
        controls.removeFromRight (6);
    }
    timeArea = controls.removeFromRight (actionButton.isVisible() ? 0 : 86);
    volume.setBounds (controls.removeFromLeft (juce::jmin (110, controls.getWidth())));
    r.removeFromTop (6);
    auto bottom = r.removeFromBottom (32);
    r.removeFromBottom (6);
    list.setBounds (r);
    nextButton.setBounds (bottom.removeFromLeft (44));
    bottom.removeFromLeft (6);
    clearButton.setBounds (bottom.removeFromLeft (44));
    bottom.removeFromLeft (6);
    settingsButton.setBounds (bottom.removeFromRight (juce::jmin (bottom.getWidth(), player.getSkipAds() ? 140 : 64)));
}

void YouTubePanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    const auto& items = queue.getItems();
    if (! juce::isPositiveAndBelow (row, (int) items.size()))
        return;
    const auto& v = items[(size_t) row];
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.0f, 2.0f);
    if (selected || row == 0)
    {
        g.setColour (selected ? DarkLookAndFeel::selection : DarkLookAndFeel::raised);
        g.fillRoundedRectangle (r, 8.0f);
    }
    r.reduce (8.0f, 0.0f);
    g.setColour (DarkLookAndFeel::textQuiet);
    g.setFont (DarkLookAndFeel::mono (12.0f));
    g.drawText (juce::String (row + 1), r.removeFromLeft (18.0f), juce::Justification::centredLeft, false);
    YouTubeThumbnails::draw (g, thumbnails->get (v.id), r.removeFromLeft (64.0f).withSizeKeepingCentre (64.0f, 36.0f), 4.0f);
    r.removeFromLeft (8.0f);
    Icons::draw (g, r.removeFromRight (16.0f).withSizeKeepingCentre (12.0f, 12.0f), Icons::Glyph::close, DarkLookAndFeel::textQuiet);
    r.removeFromRight (6.0f);
    // El titulo entero (hasta dos lineas) y debajo canal y duracion.
    const auto sub = r.removeFromBottom (16.0f);
    g.setColour (DarkLookAndFeel::text);
    g.setFont (DarkLookAndFeel::sans (12.5f, true));
    g.drawFittedText (v.title, r.toNearestInt(), juce::Justification::bottomLeft, 2, 0.85f);
    g.setColour (DarkLookAndFeel::textQuiet);
    g.setFont (DarkLookAndFeel::sans (11.5f));
    g.drawText (v.channel + (v.seconds > 0.0 ? "  " + YouTubeSearch::formatDuration (v.seconds) : juce::String()), sub,
                juce::Justification::topLeft, true);
}

void YouTubePanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    // La x de la derecha la quita.
    if (e.x >= list.getVisibleRowWidth() - 26)
        queue.remove (row);
}

void YouTubePanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    // Doble clic: que suene ya (sale de la lista).
    const auto& items = queue.getItems();
    if (! juce::isPositiveAndBelow (row, (int) items.size()))
        return;
    const auto v = items[(size_t) row];
    queue.remove (row);
    player.open (v);
}
}
