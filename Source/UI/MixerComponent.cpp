#include "UI/MixerComponent.h"
#include "UI/DarkLookAndFeel.h"
#include "UI/VideoWindow.h"

namespace midj
{
// --- Crossfader --------------------------------------------------------------------------------

CrossfaderSlider::CrossfaderSlider()
{
    setSliderStyle (juce::Slider::LinearHorizontal);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRange (0.0, 1.0);
    setValue (0.5, juce::dontSendNotification);
    setDoubleClickReturnValue (true, 0.5);
    setMouseDragSensitivity (200);
    setTooltip ("Crossfader (doble clic: al centro)");
}

void CrossfaderSlider::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (DarkLookAndFeel::background.darker (0.2f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (DarkLookAndFeel::textQuiet.withAlpha (0.35f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    // A y B en los extremos.
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (DarkLookAndFeel::textQuiet);
    auto letters = r.reduced (6.0f, 0.0f);
    g.drawText ("A", letters.removeFromLeft (12.0f), juce::Justification::centred, false);
    g.drawText ("B", letters.removeFromRight (12.0f), juce::Justification::centred, false);

    // Recorrido: a la izquierda del cursor la linea roja y a la derecha la azul, como en VirtualDJ.
    const auto track = r.reduced (24.0f, 0.0f);
    const float y = track.getCentreY() + 3.0f;
    const float x = track.getX() + track.getWidth() * (float) valueToProportionOfLength (getValue());
    g.setColour (DarkLookAndFeel::accentB);
    g.fillRect (juce::Rectangle<float> (track.getX(), y - 1.5f, x - track.getX(), 3.0f));
    g.setColour (DarkLookAndFeel::accentA);
    g.fillRect (juce::Rectangle<float> (x, y - 1.5f, track.getRight() - x, 3.0f));

    // Marca del centro (triangulo arriba).
    juce::Path mark;
    const float cx = track.getCentreX();
    mark.addTriangle (cx - 4.0f, r.getY() + 2.0f, cx + 4.0f, r.getY() + 2.0f, cx, r.getY() + 7.0f);
    g.setColour (DarkLookAndFeel::textQuiet);
    g.fillPath (mark);

    // Cursor: una pieza gris con una raya en medio.
    const auto thumb = juce::Rectangle<float> (14.0f, juce::jmin (r.getHeight() - 8.0f, 22.0f)).withCentre ({ x, y });
    g.setColour (juce::Colour (0xff5a5f69));
    g.fillRoundedRectangle (thumb, 2.0f);
    g.setColour (juce::Colour (0xff2b2f36));
    g.drawRoundedRectangle (thumb, 2.0f, 1.0f);
    g.setColour (DarkLookAndFeel::text);
    g.drawVerticalLine ((int) x, thumb.getY() + 3.0f, thumb.getBottom() - 3.0f);
}

// --- Mezclador ---------------------------------------------------------------------------------

MixerComponent::MixerComponent (Mixer& m) : mixer (m)
{
    setUpChannel (channels[0], mixer.getStrip (0), DarkLookAndFeel::accentA);
    setUpChannel (channels[1], mixer.getStrip (1), DarkLookAndFeel::accentB);

    for (size_t i = 0; i < rowLabels.size(); ++i)
    {
        rowLabels[i].setText (knobNames[i], juce::dontSendNotification);
        rowLabels[i].setJustificationType (juce::Justification::centred);
        rowLabels[i].setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
        rowLabels[i].setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (rowLabels[i]);
    }
    volumeLabel.setText ("VOL", juce::dontSendNotification);
    volumeLabel.setJustificationType (juce::Justification::centred);
    volumeLabel.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
    volumeLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (volumeLabel);

    // Pestanas AUDIO / VIDEO.
    for (auto* b : { &audioTabButton, &videoTabButton })
    {
        b->setClickingTogglesState (false);
        b->setRadioGroupId (0);
        b->setColour (juce::TextButton::buttonColourId, DarkLookAndFeel::panel);
        b->setColour (juce::TextButton::buttonOnColourId, DarkLookAndFeel::panel.brighter (0.25f));
        b->setColour (juce::TextButton::textColourOffId, DarkLookAndFeel::textQuiet);
        b->setColour (juce::TextButton::textColourOnId, DarkLookAndFeel::text);
        addAndMakeVisible (b);
    }
    audioTabButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    videoTabButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    audioTabButton.setTooltip ("EQ, filtro y volumen de cada deck (tecla V: cambiar de pestana)");
    videoTabButton.setTooltip ("Vista previa del video, nivel de video de cada deck y transicion (tecla V)");
    audioTabButton.onClick = [this] { showVideoTab (false); };
    videoTabButton.onClick = [this] { showVideoTab (true); };

    // Pestana VIDEO: nivel de video de cada deck, transicion entre escenas.
    for (size_t c = 0; c < channels.size(); ++c)
    {
        auto& v = channels[c].videoLevel;
        v.setSliderStyle (juce::Slider::LinearVertical);
        v.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        v.setRange (0.0, 1.0);
        v.setValue (1.0, juce::dontSendNotification);
        v.setDoubleClickReturnValue (true, 1.0);
        v.setTooltip ("Nivel de video del deck " + juce::String (c == 0 ? "A" : "B") + " (0 = no se ve)");
        v.onValueChange = [this, c, &v] { if (stage != nullptr) stage->getMix().setLevel ((int) c, (float) v.getValue()); };
        addChildComponent (v);
    }
    transitionLabel.setJustificationType (juce::Justification::centred);
    transitionLabel.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
    transitionLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    addChildComponent (transitionLabel);
    for (int t = 0; t <= (int) TransitionType::swipe; ++t)
        transitionType.addItem (SceneCollection::transitionName ((TransitionType) t), t + 1);
    transitionType.setTooltip ("Transicion al cambiar de escena");
    transitionType.onChange = [this]
    {
        if (stage == nullptr) return;
        stage->getScenes().transition.type = (TransitionType) (transitionType.getSelectedId() - 1);
        stage->scenesChanged();
    };
    addChildComponent (transitionType);
    transitionMs.setSliderStyle (juce::Slider::LinearHorizontal);
    transitionMs.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 18);
    transitionMs.setRange (0.0, 3000.0, 50.0);
    transitionMs.setTextValueSuffix (" ms");
    transitionMs.setTooltip ("Duracion de la transicion");
    transitionMs.onValueChange = [this]
    {
        if (stage == nullptr) return;
        stage->getScenes().transition.milliseconds = (int) transitionMs.getValue();
        stage->scenesChanged();
    };
    addChildComponent (transitionMs);
    noVideoLabel.setJustificationType (juce::Justification::centred);
    noVideoLabel.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
    addChildComponent (noVideoLabel);

    crossfader.onValueChange = [this] { mixer.setCrossfader ((float) crossfader.getValue()); };
    addAndMakeVisible (crossfader);

    showVideoTab (false);
    startTimerHz (15);
}

MixerComponent::~MixerComponent()
{
    setVideoStage (nullptr);
}

void MixerComponent::setVideoStage (VideoStage* s)
{
    if (stage != nullptr)
    {
        stage->setPreviewShowing (false);
        removeChildComponent (&stage->getPreview());
    }
    stage = s;
    if (stage != nullptr)
    {
        addChildComponent (stage->getPreview());
        refreshTransition();
    }
    showVideoTab (videoTab);
}

void MixerComponent::refreshTransition()
{
    if (stage == nullptr)
        return;
    const auto& t = stage->getScenes().transition;
    transitionType.setSelectedId ((int) t.type + 1, juce::dontSendNotification);
    transitionMs.setValue (t.milliseconds, juce::dontSendNotification);
}

void MixerComponent::showVideoTab (bool video)
{
    videoTab = video;
    audioTabButton.setToggleState (! video, juce::dontSendNotification);
    videoTabButton.setToggleState (video, juce::dontSendNotification);
    for (auto& c : channels)
    {
        for (auto& k : c.knobs)
            k.setVisible (! video);
        c.volume.setVisible (! video);
        c.videoLevel.setVisible (video);
    }
    for (auto& l : rowLabels)
        l.setVisible (! video);
    volumeLabel.setVisible (! video);
    const bool hasVideo = stage != nullptr && stage->isReady();
    for (auto* c : std::initializer_list<juce::Component*> { &transitionLabel, &transitionType, &transitionMs })
        c->setVisible (video && stage != nullptr);
    noVideoLabel.setText (stage != nullptr ? "Sin Direct3D 11: " + stage->getError() : juce::String ("Video no disponible"),
                          juce::dontSendNotification);
    noVideoLabel.setVisible (video && ! hasVideo);
    if (stage != nullptr)
    {
        refreshTransition();
        stage->getPreview().setVisible (video && hasVideo);
        stage->setPreviewShowing (video && hasVideo);   // sin la pestana a la vista no se compone
    }
    resized();
    repaint();
}

void MixerComponent::addLearnTargets (std::vector<LearnTarget>& out)
{
    static constexpr std::array<const char*, 5> verbs { "gain", "high", "mid", "low", "filter" };
    for (size_t c = 0; c < channels.size(); ++c)
    {
        const juce::String prefix = "deck" + juce::String ((int) c + 1) + ".";
        for (size_t i = 0; i < verbs.size(); ++i)
            out.push_back ({ &channels[c].knobs[i], prefix + verbs[i] });
        out.push_back ({ &channels[c].volume, prefix + "volume" });
    }
    out.push_back ({ &crossfader, "crossfader" });
}

void MixerComponent::timerCallback()
{
    for (size_t c = 0; c < channels.size(); ++c)
    {
        auto& strip = mixer.getStrip ((int) c);
        const std::array<float, 5> values { strip.getGain(), strip.getHigh(), strip.getMid(), strip.getLow(), strip.getFilter() };
        for (size_t i = 0; i < values.size(); ++i)
            if (! channels[c].knobs[i].isMouseButtonDown())
                channels[c].knobs[i].setValue (values[i], juce::dontSendNotification);
        if (! channels[c].volume.isMouseButtonDown())
            channels[c].volume.setValue (strip.getVolume(), juce::dontSendNotification);
    }
    // El crossfader sigue a la controladora y al automix.
    if (! crossfader.isMouseButtonDown())
        crossfader.setValue (mixer.getCrossfader(), juce::dontSendNotification);
}

void MixerComponent::setUpChannel (Channel& channel, ChannelStrip& strip, juce::Colour accent)
{
    // Orden de los mandos = knobNames: GAIN, HIGH, MID, LOW, FILTER.
    const std::array<std::function<void (float)>, 5> setters {
        [&strip] (float v) { strip.setGain (v); },  [&strip] (float v) { strip.setHigh (v); },
        [&strip] (float v) { strip.setMid (v); },   [&strip] (float v) { strip.setLow (v); },
        [&strip] (float v) { strip.setFilter (v); } };
    const std::array<float, 5> values { strip.getGain(), strip.getHigh(), strip.getMid(), strip.getLow(), strip.getFilter() };

    for (size_t i = 0; i < channel.knobs.size(); ++i)
    {
        auto& k = channel.knobs[i];
        k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setRange (0.0, 1.0);
        k.setValue (values[i], juce::dontSendNotification);
        k.setDoubleClickReturnValue (true, ChannelStrip::defaultKnob);
        k.setColour (juce::Slider::rotarySliderFillColourId, accent);
        k.onValueChange = [&k, set = setters[i]] { set ((float) k.getValue()); };
        addAndMakeVisible (k);
    }

    auto& v = channel.volume;
    v.setSliderStyle (juce::Slider::LinearVertical);
    v.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    v.setRange (0.0, 1.0);
    v.setValue (strip.getVolume(), juce::dontSendNotification);
    v.setDoubleClickReturnValue (true, ChannelStrip::defaultVolume);
    v.onValueChange = [&v, &strip] { strip.setVolume ((float) v.getValue()); };
    addAndMakeVisible (v);
}

void MixerComponent::paint (juce::Graphics& g)
{
    g.setColour (DarkLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
    if (videoTab)
    {
        // Barra del color de cada deck bajo su fader de video (como en VirtualDJ).
        for (size_t c = 0; c < channels.size(); ++c)
        {
            const auto b = channels[c].videoLevel.getBounds().toFloat();
            g.setColour ((c == 0 ? DarkLookAndFeel::accentA : DarkLookAndFeel::accentB).withAlpha (0.55f));
            g.fillRect (b.withSizeKeepingCentre (3.0f, b.getHeight() - 16.0f));
        }
        g.setColour (juce::Colours::black);
        g.fillRect (previewArea);
    }
}

void MixerComponent::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto tabs = area.removeFromTop (24);
    audioTabButton.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2));
    videoTabButton.setBounds (tabs);
    area.removeFromTop (6);
    crossfader.setBounds (area.removeFromBottom (38));
    area.removeFromBottom (6);

    // Los faders van a los lados en toda la altura (como en una mesa).
    constexpr int faderWidth = 34;
    auto faderA = area.removeFromLeft (faderWidth);
    auto faderB = area.removeFromRight (faderWidth);
    channels[0].videoLevel.setBounds (faderA);
    channels[1].videoLevel.setBounds (faderB);

    if (videoTab)
    {
        // VIDEO: la vista previa (16:9) arriba y la transicion debajo.
        auto centre = area.reduced (4, 0);
        const int h = juce::jmin (centre.getHeight() - 70, centre.getWidth() * 9 / 16);
        previewArea = centre.removeFromTop (h).withSizeKeepingCentre (h * 16 / 9, h);
        if (stage != nullptr)
            stage->getPreview().setBounds (previewArea);
        noVideoLabel.setBounds (previewArea);
        centre.removeFromTop (10);
        transitionLabel.setBounds (centre.removeFromTop (16));
        transitionType.setBounds (centre.removeFromTop (24));
        centre.removeFromTop (4);
        transitionMs.setBounds (centre.removeFromTop (22));
        return;
    }

    // AUDIO: cinco columnas: fader A | mandos A | nombres | mandos B | fader B.
    volumeLabel.setBounds (faderA.removeFromBottom (18).withWidth (faderWidth));
    faderB.removeFromBottom (18);
    channels[0].volume.setBounds (faderA);
    channels[1].volume.setBounds (faderB);
    const int colWidth = juce::jmin (area.getWidth() / 3, area.getHeight() / (int) knobNames.size() + 8);
    const int rowHeight = area.getHeight() / (int) knobNames.size();
    for (size_t i = 0; i < knobNames.size(); ++i)
    {
        auto row = area.removeFromTop (rowHeight);
        channels[0].knobs[i].setBounds (row.removeFromLeft (colWidth).reduced (2));
        channels[1].knobs[i].setBounds (row.removeFromRight (colWidth).reduced (2));
        rowLabels[i].setBounds (row);
    }
}
}
