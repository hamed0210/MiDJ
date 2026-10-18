#include "UI/MixerComponent.h"
#include "UI/DarkLookAndFeel.h"
#include "UI/VideoWindow.h"

namespace midj
{
namespace
{
    juce::String dbText (float gain)
    {
        if (gain <= 0.0005f)
            return "corte";
        const float db = juce::Decibels::gainToDecibels (gain);
        if (std::abs (db) < 0.05f)
            return "0 dB";
        return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
    }

    juce::String filterText (float knob)
    {
        const auto f = ChannelStrip::filterFromKnob (knob);
        if (! f.active)
            return "off";
        const auto hz = f.cutoffHz >= 1000.0f ? juce::String (f.cutoffHz / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (f.cutoffHz));
        return (f.lowPass ? "LP " : "HP ") + hz;
    }

    juce::Colour deckColour (size_t c) { return DarkLookAndFeel::deckColour ((int) c); }
}

MixerComponent::MixerComponent (Mixer& m) : mixer (m)
{
    setUpChannel (0);
    setUpChannel (1);

    master.slider.setColour (juce::Slider::rotarySliderFillColourId, DarkLookAndFeel::text);
    master.slider.setValue (mixer.getMasterVolume(), juce::dontSendNotification);
    master.formatValue = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    master.slider.onValueChange = [this] { mixer.setMasterVolume ((float) master.slider.getValue()); };
    master.slider.setTooltip ("Volumen del master (doble clic: 80 %, sin cambio)");
    addChildComponent (master);
    addChildComponent (masterLeft);
    addChildComponent (masterRight);

    // Pestanas AUDIO / PROGRAMA.
    for (auto* b : { &audioTabButton, &videoTabButton })
    {
        b->setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (b);
    }
    audioTabButton.setTooltip ("EQ, filtro y vumetros de cada deck y el master (tecla V: cambiar de pestana)");
    videoTabButton.setTooltip ("La salida de video y el nivel de video de cada deck (tecla V)");
    audioTabButton.onClick = [this] { showVideoTab (false); };
    videoTabButton.onClick = [this] { showVideoTab (true); };

    noVideoLabel.setJustificationType (juce::Justification::centred);
    noVideoLabel.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
    addChildComponent (noVideoLabel);

    crossfader.setTooltip ("Crossfader (doble clic: al centro). Flechas: moverlo; Mayus+flecha: del todo");
    crossfader.setMouseDragSensitivity (200);
    crossfader.onValueChange = [this] { mixer.setCrossfader ((float) crossfader.getValue()); repaint (centreLabelArea); };
    addAndMakeVisible (crossfader);

    addTransitionButton.setColour (juce::TextButton::textColourOffId, DarkLookAndFeel::custom.brighter (0.3f));
    addTransitionButton.setTooltip ("Crear una transicion propia: stinger (video), barrido con imagen o fundido a un color");
    addTransitionButton.onClick = [this] { if (onAddCustomTransition) onAddCustomTransition(); };
    addAndMakeVisible (addTransitionButton);
    transitionMs.setSliderStyle (juce::Slider::LinearHorizontal);
    transitionMs.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    transitionMs.setRange (100.0, 3000.0, 50.0);
    transitionMs.setTooltip ("Duracion de la transicion");
    transitionMs.onValueChange = [this]
    {
        if (stage == nullptr) return;
        stage->getScenes().transition.milliseconds = (int) transitionMs.getValue();
        stage->scenesChanged();
        repaint (msLabelArea);
    };
    addAndMakeVisible (transitionMs);

    refreshTransitions();
    showVideoTab (false);
    startTimerHz (30);
}

MixerComponent::~MixerComponent()
{
    setVideoStage (nullptr);
}

void MixerComponent::setUpChannel (int index)
{
    auto& channel = channels[(size_t) index];
    auto& strip = mixer.getStrip (index);
    const auto accent = deckColour ((size_t) index);
    // Orden de la pantalla: HIGH, MID, LOW, GAIN, FILTER.
    const std::array<const char*, 5> names { "High", "Mid", "Low", "Gain", "Filter" };
    const std::array<std::function<void (float)>, 5> setters {
        [&strip] (float v) { strip.setHigh (v); }, [&strip] (float v) { strip.setMid (v); },
        [&strip] (float v) { strip.setLow (v); },  [&strip] (float v) { strip.setGain (v); },
        [&strip] (float v) { strip.setFilter (v); } };
    const std::array<std::function<juce::String (double)>, 5> formats {
        [] (double v) { return dbText (ChannelStrip::bandGainFromKnob ((float) v)); },
        [] (double v) { return dbText (ChannelStrip::bandGainFromKnob ((float) v)); },
        [] (double v) { return dbText (ChannelStrip::bandGainFromKnob ((float) v)); },
        [] (double v) { return dbText (ChannelStrip::gainFromKnob ((float) v)); },
        [] (double v) { return filterText ((float) v); } };
    const std::array<const char*, 5> tips { "Agudos", "Medios", "Graves", "Ganancia", "Filtro: a la izquierda deja los graves, a la derecha los agudos" };

    for (size_t i = 0; i < channel.knobs.size(); ++i)
    {
        auto& k = channel.knobs[i];
        k = std::make_unique<LabelledKnob> (names[i], 0.0, 1.0, ChannelStrip::defaultKnob);
        k->formatValue = formats[i];
        k->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        k->slider.setTooltip (juce::String (tips[i]) + " del deck " + (index == 0 ? "A" : "B") + " (doble clic: al centro)");
        k->slider.onValueChange = [&s = k->slider, set = setters[i]] { set ((float) s.getValue()); };
        addChildComponent (*k);
    }
    addChildComponent (channel.meter);

    auto& v = channel.volume;
    v.setSliderStyle (juce::Slider::LinearVertical);
    v.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    v.setRange (0.0, 1.0);
    v.setValue (strip.getVolume(), juce::dontSendNotification);
    v.setDoubleClickReturnValue (true, ChannelStrip::defaultVolume);
    v.setColour (juce::Slider::trackColourId, accent);
    v.setTooltip (juce::String ("Volumen del deck ") + (index == 0 ? "A" : "B"));
    v.onValueChange = [this, &v, &strip] { strip.setVolume ((float) v.getValue()); repaint (faderLabelArea[0].getUnion (faderLabelArea[1])); };
    addAndMakeVisible (v);
    addAndMakeVisible (channel.faderMeter);

    auto& level = channel.videoLevel;
    level.setSliderStyle (juce::Slider::LinearHorizontal);
    level.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    level.setRange (0.0, 1.0);
    level.setValue (1.0, juce::dontSendNotification);
    level.setDoubleClickReturnValue (true, 1.0);
    level.setColour (juce::Slider::trackColourId, accent);
    level.setTooltip (juce::String ("Nivel de video del deck ") + (index == 0 ? "A" : "B") + " (0 = no se ve)");
    level.onValueChange = [this, index, &level]
    {
        if (stage != nullptr) stage->getMix().setLevel (index, (float) level.getValue());
        repaint (videoLevelLabel[(size_t) index]);
    };
    addChildComponent (level);
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
        addChildComponent (stage->getPreview());
    refreshTransitions();
    showVideoTab (videoTab);
}

void MixerComponent::refreshTransitions()
{
    for (auto& b : transitionButtons)
        removeChildComponent (b.get());
    transitionButtons.clear();
    for (int t = 0; t <= (int) TransitionType::swipe; ++t)
    {
        auto b = std::make_unique<juce::TextButton> (t == (int) TransitionType::fadeToBlack ? juce::String ("A negro")
                                                                                          : SceneCollection::transitionName ((TransitionType) t));
        b->onClick = [this, t] { selectTransition (t); };
        addAndMakeVisible (*b);
        transitionButtons.push_back (std::move (b));
    }
    if (stage != nullptr)
    {
        const auto& tr = stage->getScenes().transition;
        transitionMs.setValue (tr.milliseconds, juce::dontSendNotification);
        transitionMs.setEnabled (tr.type != TransitionType::cut);
        for (size_t i = 0; i < transitionButtons.size(); ++i)
        {
            const bool on = (int) i == (int) tr.type;
            transitionButtons[i]->setColour (juce::TextButton::buttonColourId, on ? DarkLookAndFeel::selection : juce::Colours::transparentBlack);
            transitionButtons[i]->setColour (juce::TextButton::textColourOffId, on ? DarkLookAndFeel::text : DarkLookAndFeel::textQuiet);
            transitionButtons[i]->setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        }
    }
    for (auto& b : transitionButtons)
        b->setEnabled (stage != nullptr);
    transitionMs.setVisible (stage != nullptr);
    resized();
    repaint();
}

void MixerComponent::selectTransition (int index)
{
    if (stage == nullptr)
        return;
    stage->getScenes().transition.type = (TransitionType) index;
    stage->scenesChanged();
    refreshTransitions();
}

void MixerComponent::showVideoTab (bool video)
{
    videoTab = video;
    for (auto [b, on] : { std::pair<juce::TextButton*, bool> { &audioTabButton, ! video }, std::pair<juce::TextButton*, bool> { &videoTabButton, video } })
    {
        b->setColour (juce::TextButton::buttonColourId, on ? DarkLookAndFeel::line : juce::Colours::transparentBlack);
        b->setColour (juce::TextButton::textColourOffId, on ? DarkLookAndFeel::text : DarkLookAndFeel::textQuiet);
    }
    for (auto& c : channels)
    {
        for (auto& k : c.knobs)
            k->setVisible (! video);
        c.meter.setVisible (! video);
        c.videoLevel.setVisible (video);
    }
    master.setVisible (! video);
    masterLeft.setVisible (! video);
    masterRight.setVisible (! video);
    const bool hasVideo = stage != nullptr && stage->isReady();
    noVideoLabel.setText (stage != nullptr ? "Sin Direct3D 11: " + stage->getError() : juce::String ("Video no disponible"),
                          juce::dontSendNotification);
    noVideoLabel.setVisible (video && ! hasVideo);
    if (stage != nullptr)
    {
        stage->getPreview().setVisible (video && hasVideo);
        stage->setPreviewShowing (video && hasVideo);   // sin la pestana a la vista no se compone
    }
    resized();
    repaint();
}

void MixerComponent::addLearnTargets (std::vector<LearnTarget>& out)
{
    static constexpr std::array<const char*, 5> verbs { "high", "mid", "low", "gain", "filter" };
    for (size_t c = 0; c < channels.size(); ++c)
    {
        const juce::String prefix = "deck" + juce::String ((int) c + 1) + ".";
        for (size_t i = 0; i < verbs.size(); ++i)
            out.push_back ({ &channels[c].knobs[i]->slider, prefix + verbs[i] });
        out.push_back ({ &channels[c].volume, prefix + "volume" });
    }
    out.push_back ({ &crossfader, "crossfader" });
}

juce::String MixerComponent::centreText() const
{
    const double x = crossfader.getValue();
    if (crossfader.isAtCentre())
        return "CENTRO";
    const int percent = juce::roundToInt (std::abs (x - 0.5) * 200.0);
    return x < 0.5 ? juce::String::fromUTF8 ("\xe2\x97\x82 ") + juce::String (percent) + " % hacia A"
                   : juce::String (percent) + " % hacia B " + juce::String::fromUTF8 ("\xe2\x96\xb8");
}

void MixerComponent::timerCallback()
{
    for (size_t c = 0; c < channels.size(); ++c)
    {
        auto& strip = mixer.getStrip ((int) c);
        const std::array<float, 5> values { strip.getHigh(), strip.getMid(), strip.getLow(), strip.getGain(), strip.getFilter() };
        for (size_t i = 0; i < values.size(); ++i)
            if (! channels[c].knobs[i]->slider.isMouseButtonDown())
                channels[c].knobs[i]->slider.setValue (values[i], juce::dontSendNotification);
        if (! channels[c].volume.isMouseButtonDown())
            channels[c].volume.setValue (strip.getVolume(), juce::dontSendNotification);
        const float peak = mixer.takeDeckPeak ((int) c);
        channels[c].meter.setPeak (peak);
        channels[c].faderMeter.setPeak (peak);
    }
    masterLeft.setPeak (mixer.takeMasterPeak (0));
    masterRight.setPeak (mixer.takeMasterPeak (1));
    // El crossfader sigue a la controladora y al automix.
    if (! crossfader.isMouseButtonDown())
        crossfader.setValue (mixer.getCrossfader(), juce::dontSendNotification);
    if (const auto text = centreText(); text != lastCentreText)
    {
        lastCentreText = text;
        repaint (centreLabelArea);
    }
}

void MixerComponent::paint (juce::Graphics& g)
{
    // Fondo de las pestanas, de la pagina y de la tarjeta del crossfader.
    g.setColour (DarkLookAndFeel::panel);
    g.fillRoundedRectangle (tabsArea.toFloat(), 10.0f);
    g.fillRoundedRectangle (cardArea.toFloat(), 14.0f);
    if (! videoTab)
    {
        g.fillRoundedRectangle (pageArea.toFloat(), 14.0f);
        g.setColour (DarkLookAndFeel::line);
        g.drawRoundedRectangle (pageArea.toFloat().reduced (0.5f), 14.0f, 1.0f);
        g.setFont (DarkLookAndFeel::sans (12.0f, true).withKerningFactor (0.08f));
        for (size_t c = 0; c < channels.size(); ++c)
        {
            g.setColour (deckColour (c));
            g.drawText (c == 0 ? "DECK A" : "DECK B", stripLabelArea[c], c == 0 ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
        }
        g.setColour (DarkLookAndFeel::text);
        g.drawText ("MASTER", masterLabelArea, juce::Justification::centred, false);
        // Separadores del master.
        g.setColour (DarkLookAndFeel::selection);
        g.fillRect (masterLabelArea.getX() - 8, pageArea.getY() + 14, 1, pageArea.getHeight() - 28);
        g.fillRect (masterLabelArea.getRight() + 8, pageArea.getY() + 14, 1, pageArea.getHeight() - 28);
    }
    else
    {
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (previewArea.toFloat(), 10.0f);
        g.setColour (DarkLookAndFeel::panel);
        g.fillRoundedRectangle (videoLabelArea.toFloat(), 10.0f);
        g.setFont (DarkLookAndFeel::sans (12.0f, true));
        for (size_t c = 0; c < channels.size(); ++c)
        {
            auto r = videoLevelLabel[c];
            g.setColour (deckColour (c));
            g.drawText (c == 0 ? "Video A" : "Video B", r.removeFromLeft (56), juce::Justification::centredLeft, false);
            g.setColour (DarkLookAndFeel::text);
            g.setFont (DarkLookAndFeel::mono (12.0f));
            g.drawText (juce::String (juce::roundToInt (channels[c].videoLevel.getValue() * 100.0)) + " %", r.removeFromRight (44),
                        juce::Justification::centredRight, false);
            g.setFont (DarkLookAndFeel::sans (12.0f, true));
        }
    }

    // Tarjeta: VOL A / VOL B con su valor, A y B junto al crossfader, el texto del centro y la transicion.
    for (size_t c = 0; c < channels.size(); ++c)
    {
        auto r = faderLabelArea[c];
        g.setColour (deckColour (c));
        g.setFont (DarkLookAndFeel::sans (11.0f, true));
        g.drawText (c == 0 ? "VOL A" : "VOL B", r.removeFromTop (14), juce::Justification::centred, false);
        g.setColour (DarkLookAndFeel::textQuiet);
        g.setFont (DarkLookAndFeel::mono (11.0f));
        g.drawText (juce::String (juce::roundToInt (channels[c].volume.getValue() * 100.0)) + " %", r, juce::Justification::centred, false);
    }
    g.setFont (DarkLookAndFeel::sans (14.0f, true));
    g.setColour (DarkLookAndFeel::accentA);
    g.drawText ("A", crossLabelsA, juce::Justification::centred, false);
    g.setColour (DarkLookAndFeel::accentB);
    g.drawText ("B", crossLabelsB, juce::Justification::centred, false);
    g.setColour (crossfader.isAtCentre() ? DarkLookAndFeel::good : DarkLookAndFeel::textFaint);
    g.setFont (DarkLookAndFeel::sans (10.0f, true).withKerningFactor (0.08f));
    g.drawText (centreText(), centreLabelArea, juce::Justification::centred, false);
    g.setColour (DarkLookAndFeel::textQuiet);
    g.setFont (DarkLookAndFeel::sans (12.0f));
    g.drawText ("Transicion", transitionLabelArea, juce::Justification::centredLeft, false);
    if (stage != nullptr)
    {
        const auto& tr = stage->getScenes().transition;
        g.setColour (DarkLookAndFeel::text);
        g.setFont (DarkLookAndFeel::mono (12.0f));
        g.drawText (tr.type == TransitionType::cut ? juce::String ("al instante") : juce::String (tr.milliseconds) + " ms",
                    msLabelArea, juce::Justification::centredRight, false);
    }
}

void MixerComponent::resized()
{
    auto area = getLocalBounds();

    // Pestanas.
    tabsArea = area.removeFromTop (34);
    auto tabs = tabsArea.reduced (4);
    audioTabButton.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2));
    videoTabButton.setBounds (tabs);
    area.removeFromTop (10);

    // Tarjeta del crossfader abajo.
    cardArea = area.removeFromBottom (area.getHeight() >= 420 ? 164 : 136);
    area.removeFromBottom (10);
    pageArea = area;

    {
        auto card = cardArea.reduced (14, 12);
        for (size_t c = 0; c < channels.size(); ++c)
        {
            auto col = c == 0 ? card.removeFromLeft (46) : card.removeFromRight (46);
            faderLabelArea[c] = col.removeFromBottom (30);
            col.removeFromBottom (4);
            auto meter = c == 0 ? col.removeFromRight (6) : col.removeFromLeft (6);
            channels[c].faderMeter.setBounds (meter);
            channels[c].volume.setBounds (col.reduced (2, 0));
        }
        card.reduce (14, 0);
        auto cross = card.removeFromTop (30);
        crossLabelsA = cross.removeFromLeft (18);
        crossLabelsB = cross.removeFromRight (18);
        crossfader.setBounds (cross.reduced (6, 0));
        centreLabelArea = card.removeFromTop (14);
        card.removeFromTop (8);

        // Botones de transicion (pueden ocupar dos lineas) y la duracion.
        auto msRow = card.removeFromBottom (24);
        transitionLabelArea = msRow.removeFromLeft (70);
        msLabelArea = msRow.removeFromRight (76);
        transitionMs.setBounds (msRow.reduced (4, 2));
        card.removeFromBottom (6);
        juce::Font font (DarkLookAndFeel::sans (12.0f, true));
        int x = card.getX(), y = card.getY();
        const int rowH = 28;
        std::vector<juce::Button*> all;
        for (auto& b : transitionButtons) all.push_back (b.get());
        all.push_back (&addTransitionButton);
        for (auto* b : all)
        {
            const int w = juce::GlyphArrangement::getStringWidthInt (font, b->getButtonText()) + 22;
            if (x + w > card.getRight() && x > card.getX())
            {
                x = card.getX();
                y += rowH + 4;
            }
            b->setBounds (x, y, w, rowH);
            b->setVisible (y + rowH <= card.getBottom() + 2);
            x += w + 4;
        }
    }

    // Pagina AUDIO: tira A | master | tira B.
    {
        auto page = pageArea.reduced (14, 12);
        auto centre = page.withSizeKeepingCentre (juce::jmin (112, page.getWidth() / 4), page.getHeight());
        auto stripA = page.withRight (centre.getX() - 16);
        auto stripB = page.withLeft (centre.getRight() + 16);
        masterLabelArea = centre.removeFromTop (18);
        centre.removeFromTop (6);
        auto meters = centre.removeFromRight (22);
        masterLeft.setBounds (meters.removeFromLeft (9));
        meters.removeFromLeft (4);
        masterRight.setBounds (meters.removeFromLeft (9));
        master.setBounds (centre.removeFromTop (juce::jmin (centre.getHeight(), 88)));

        for (size_t c = 0; c < channels.size(); ++c)
        {
            auto strip = c == 0 ? stripA : stripB;
            stripLabelArea[c] = strip.removeFromTop (18);
            strip.removeFromTop (6);
            auto meter = c == 0 ? strip.removeFromRight (8) : strip.removeFromLeft (8);
            channels[c].meter.setBounds (meter);
            if (c == 0) strip.removeFromRight (10); else strip.removeFromLeft (10);
            // Dos filas (High Mid Low / Gain Filter) si hay alto; si no, los cinco en una fila.
            if (strip.getHeight() >= 170)
            {
                const int rowH = strip.getHeight() / 2;
                auto row1 = strip.removeFromTop (rowH), row2 = strip;
                const int colW = row1.getWidth() / 3;
                for (size_t i = 0; i < 3; ++i)
                    channels[c].knobs[i]->setBounds (row1.removeFromLeft (colW));
                for (size_t i = 3; i < 5; ++i)
                    channels[c].knobs[i]->setBounds (row2.removeFromLeft (colW));
            }
            else
            {
                const int colW = strip.getWidth() / 5;
                for (auto& k : channels[c].knobs)
                    k->setBounds (strip.removeFromLeft (colW));
            }
        }
    }

    // Pagina PROGRAMA: la vista previa 16:9 y los niveles de video debajo.
    {
        auto page = pageArea;
        videoLabelArea = page.removeFromBottom (34);
        page.removeFromBottom (8);
        const int h = juce::jmin (page.getHeight(), page.getWidth() * 9 / 16);
        previewArea = page.withSizeKeepingCentre (h * 16 / 9, h);
        if (stage != nullptr)
            stage->getPreview().setBounds (previewArea);
        noVideoLabel.setBounds (previewArea);
        auto levels = videoLabelArea.reduced (12, 4);
        for (size_t c = 0; c < channels.size(); ++c)
        {
            auto half = c == 0 ? levels.removeFromLeft (levels.getWidth() / 2 - 8) : levels.withTrimmedLeft (8);
            videoLevelLabel[c] = half;
            channels[c].videoLevel.setBounds (half.withTrimmedLeft (60).withTrimmedRight (48));
        }
    }
}
}
