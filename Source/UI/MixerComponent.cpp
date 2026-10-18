#include "UI/MixerComponent.h"
#include "UI/DarkLookAndFeel.h"
#include "UI/TransitionDialog.h"
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

    // Audifonos: mezcla pre-escucha / master y volumen (solo con audifonos configurados).
    cueMixKnob.slider.setColour (juce::Slider::rotarySliderFillColourId, DarkLookAndFeel::text);
    cueMixKnob.formatValue = [] (double v)
    {
        return v < 0.02 ? juce::String ("cue") : v > 0.98 ? juce::String ("master") : juce::String (juce::roundToInt (v * 100.0)) + " % mst";
    };
    cueMixKnob.slider.setTooltip ("Audifonos: a la izquierda solo la pre-escucha, a la derecha solo el master");
    cueMixKnob.slider.onValueChange = [this] { mixer.setCueMix ((float) cueMixKnob.slider.getValue()); };
    headphoneKnob.slider.setColour (juce::Slider::rotarySliderFillColourId, DarkLookAndFeel::text);
    headphoneKnob.formatValue = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    headphoneKnob.slider.setTooltip ("Volumen de los audifonos");
    headphoneKnob.slider.onValueChange = [this] { mixer.setHeadphoneVolume ((float) headphoneKnob.slider.getValue()); };
    addChildComponent (cueMixKnob);
    addChildComponent (headphoneKnob);

    noVideoLabel.setJustificationType (juce::Justification::centred);
    noVideoLabel.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
    addChildComponent (noVideoLabel);

    crossfader.setTooltip ("Crossfader (doble clic: al centro). Flechas: moverlo; Mayus+flecha: del todo");
    crossfader.setMouseDragSensitivity (200);
    crossfader.onValueChange = [this] { mixer.setCrossfader ((float) crossfader.getValue()); repaint (centreLabelArea); };
    addAndMakeVisible (crossfader);

    addTransitionButton.setColour (juce::TextButton::textColourOffId, DarkLookAndFeel::custom.brighter (0.3f));
    addTransitionButton.setTooltip ("Crear una transicion propia: stinger (video), barrido con imagen o fundido a un color");
    addTransitionButton.onClick = [this] { addCustomTransition(); };
    addAndMakeVisible (addTransitionButton);
    // Duracion: un boton con el valor; al pulsarlo, la lista de duraciones.
    transitionMs.setTooltip ("Duracion de la transicion");
    transitionMs.onClick = [this]
    {
        if (stage == nullptr)
            return;
        const int current = stage->getScenes().transition.milliseconds;
        juce::PopupMenu menu;
        menu.addSectionHeader ("Duracion de la transicion");
        for (int ms : { 250, 400, 600, 800, 1000, 1500, 2000, 3000 })
            menu.addItem (ms, juce::String (ms) + " ms", true, ms == current);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&transitionMs),
                            [safe = juce::Component::SafePointer<MixerComponent> (this)] (int ms)
                            {
                                if (safe == nullptr || safe->stage == nullptr || ms <= 0)
                                    return;
                                safe->stage->getScenes().transition.milliseconds = ms;
                                safe->stage->scenesChanged();
                                safe->refreshTransitions();
                            });
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
    v.onValueChange = [this, index, &v, &strip] { strip.setVolume ((float) v.getValue()); repaint (faderLabelArea[(size_t) index]); };
    addAndMakeVisible (v);
    addAndMakeVisible (channel.faderMeter);

    // Pre-escucha: escuchar el deck en los audifonos (antes de su volumen).
    channel.pfl = std::make_unique<IconButton> (Icons::Glyph::headphones,
                                                juce::String ("Pre-escucha del deck ") + (index == 0 ? "A" : "B") + " en los audifonos");
    channel.pfl->onClick = [this, index]
    {
        mixer.setPfl (index, ! mixer.getPfl (index));
        updatePflButtons();
    };
    addChildComponent (*channel.pfl);
}

void MixerComponent::setHeadphonesAvailable (bool available)
{
    headphonesAvailable = available;
    for (auto& c : channels)
        c.pfl->setVisible (available);
    updatePflButtons();
    showVideoTab (videoTab);   // los mandos de los audifonos van en la pagina AUDIO
}

void MixerComponent::updatePflButtons()
{
    for (size_t c = 0; c < channels.size(); ++c)
        channels[c].pfl->setBordered (true, mixer.getPfl ((int) c) ? deckColour (c) : juce::Colour());
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
    for (auto& b : removeButtons)
        if (b != nullptr)
            removeChildComponent (b.get());
    transitionButtons.clear();
    transitionCustomIds.clear();
    removeButtons.clear();

    auto addButton = [this] (const juce::String& text, int customId, std::function<void()> onClick)
    {
        auto b = std::make_unique<juce::TextButton> (text);
        b->onClick = std::move (onClick);
        b->setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (*b);
        transitionButtons.push_back (std::move (b));
        transitionCustomIds.push_back (customId);
        std::unique_ptr<IconButton> remove;
        if (customId != 0)
        {
            remove = std::make_unique<IconButton> (Icons::Glyph::close, "Quitar esta transicion");
            remove->onClick = [this, customId]
            {
                if (stage != nullptr)
                    stage->removeCustomTransition (customId);
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MixerComponent> (this)]
                {
                    if (safe != nullptr)
                        safe->refreshTransitions();
                });
            };
            addAndMakeVisible (*remove);
        }
        removeButtons.push_back (std::move (remove));
    };
    for (int t = 0; t <= (int) TransitionType::swipe; ++t)
        addButton (t == (int) TransitionType::fadeToBlack ? juce::String ("A negro") : SceneCollection::transitionName ((TransitionType) t), 0,
                   [this, t] { selectTransition (t); });
    if (stage != nullptr)
        for (const auto& c : stage->getScenes().customTransitions)
            addButton (c.name, c.id, [this, id = c.id] { selectCustom (id); });

    if (stage != nullptr)
    {
        const auto& tr = stage->getScenes().transition;
        const auto* own = stage->getScenes().currentCustom();
        transitionMs.setButtonText (tr.type == TransitionType::cut && own == nullptr ? juce::String ("al instante") : juce::String (tr.milliseconds) + " ms");
        transitionMs.setEnabled (own == nullptr && tr.type != TransitionType::cut);
        for (size_t i = 0; i < transitionButtons.size(); ++i)
        {
            const bool isCustom = transitionCustomIds[i] != 0;
            const bool on = own != nullptr ? transitionCustomIds[i] == own->id : (! isCustom && (int) i == (int) tr.type);
            auto& b = *transitionButtons[i];
            b.setColour (juce::TextButton::buttonColourId, on ? DarkLookAndFeel::selection : juce::Colours::transparentBlack);
            b.setColour (juce::TextButton::textColourOffId, on ? DarkLookAndFeel::text
                                                               : isCustom ? DarkLookAndFeel::custom.brighter (0.3f) : DarkLookAndFeel::textQuiet);
            if (isCustom)
                b.setTooltip (SceneCollection::customKindName (stage->getScenes().findCustom (transitionCustomIds[i])->kind) + " (transicion propia)");
        }
    }
    for (auto& b : transitionButtons)
        b->setEnabled (stage != nullptr);
    addTransitionButton.setEnabled (stage != nullptr && stage->isReady());
    transitionMs.setVisible (stage != nullptr);
    resized();
    repaint();
}

void MixerComponent::selectTransition (int index)
{
    if (stage == nullptr)
        return;
    stage->getScenes().transition.type = (TransitionType) index;
    stage->getScenes().transition.custom = 0;
    stage->scenesChanged();
    refreshTransitions();
}

void MixerComponent::selectCustom (int id)
{
    if (stage == nullptr)
        return;
    stage->getScenes().transition.custom = id;
    stage->scenesChanged();
    refreshTransitions();
}

void MixerComponent::addCustomTransition()
{
    if (stage == nullptr)
        return;
    juce::Component::SafePointer<MixerComponent> safe (this);
    TransitionDialog::show (getTopLevelComponent(),
        [safe] (const CustomTransition& t)
        {
            // Probar: se ve en la pestana PROGRAMA, sin cambiar de escena.
            if (safe != nullptr && safe->stage != nullptr)
            {
                safe->showVideoTab (true);
                safe->stage->previewTransition (t);
            }
        },
        [safe] (const CustomTransition& t)
        {
            if (safe != nullptr && safe->stage != nullptr)
            {
                safe->stage->addCustomTransition (t);
                safe->refreshTransitions();
            }
        });
}

void MixerComponent::showVideoTab (bool video)
{
    videoTab = video;
    for (auto& c : channels)
    {
        for (auto& k : c.knobs)
            k->setVisible (! video);
        c.meter.setVisible (! video);
    }
    master.setVisible (! video);
    masterLeft.setVisible (! video);
    masterRight.setVisible (! video);
    cueMixKnob.setVisible (! video && headphonesAvailable);
    headphoneKnob.setVisible (! video && headphonesAvailable);
    cueMixKnob.slider.setValue (mixer.getCueMix(), juce::dontSendNotification);
    headphoneKnob.slider.setValue (mixer.getHeadphoneVolume(), juce::dontSendNotification);
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
    if (onTabChanged)
        onTabChanged (video);
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
        if (channels[c].pfl->isVisible())
            out.push_back ({ channels[c].pfl.get(), prefix + "pfl" });
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
    // Limitador: se ve 0,6 s despues de recortar mas de 0,5 dB. Pre-escucha: sigue al MIDI.
    const auto now = juce::Time::getMillisecondCounter();
    if (mixer.takeLimiterGain() < 0.944f)
        limiterUntil = now + 600;
    if (const bool shown = now < limiterUntil; shown != limiterShown)
    {
        limiterShown = shown;
        repaint (faderLabelArea[0].getUnion (faderLabelArea[1]));
    }
    for (size_t c = 0; c < channels.size(); ++c)
        if (channels[c].pfl->isVisible() && (channels[c].pfl->getProperties()["on"].operator bool()) != mixer.getPfl ((int) c))
        {
            channels[c].pfl->getProperties().set ("on", mixer.getPfl ((int) c));
            updatePflButtons();
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
    // Tarjeta de arriba (volumenes y pagina) y franja del crossfader.
    g.setColour (DarkLookAndFeel::panel);
    g.fillRoundedRectangle (pageArea.toFloat(), 14.0f);
    g.fillRoundedRectangle (cardArea.toFloat(), 12.0f);
    g.setColour (DarkLookAndFeel::line);
    g.drawRoundedRectangle (pageArea.toFloat().reduced (0.5f), 14.0f, 1.0f);
    if (! videoTab)
    {
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
        g.fillRoundedRectangle (previewArea.toFloat(), 8.0f);
    }

    // VOL A / VOL B arriba de cada fader y su valor debajo.
    for (size_t c = 0; c < channels.size(); ++c)
    {
        g.setColour (deckColour (c));
        g.setFont (DarkLookAndFeel::sans (11.0f, true));
        g.drawText (c == 0 ? "VOL A" : "VOL B", faderTitleArea[c], juce::Justification::centred, false);
        g.setColour (limiterShown ? DarkLookAndFeel::danger : DarkLookAndFeel::textQuiet);
        g.setFont (DarkLookAndFeel::mono (11.0f, limiterShown));
        g.drawText (limiterShown ? juce::String ("LIM") : juce::String (juce::roundToInt (channels[c].volume.getValue() * 100.0)) + " %",
                    faderLabelArea[c], juce::Justification::centred, false);
    }

    // Franja: A y B junto al crossfader, el texto del centro, separador y la duracion de la transicion.
    g.setFont (DarkLookAndFeel::sans (14.0f, true));
    g.setColour (DarkLookAndFeel::accentA);
    g.drawText ("A", crossLabelsA, juce::Justification::centred, false);
    g.setColour (DarkLookAndFeel::accentB);
    g.drawText ("B", crossLabelsB, juce::Justification::centred, false);
    g.setColour (crossfader.isAtCentre() ? DarkLookAndFeel::good : DarkLookAndFeel::textFaint);
    g.setFont (DarkLookAndFeel::sans (9.5f, true).withKerningFactor (0.08f));
    g.drawText (centreText(), centreLabelArea, juce::Justification::centred, false);
    if (! separatorArea.isEmpty())
    {
        g.setColour (DarkLookAndFeel::selection);
        g.fillRect (separatorArea);
    }
}


void MixerComponent::resized()
{
    auto area = getLocalBounds();
    constexpr int gap = 8, buttonH = 28, msButtonW = 72, crossMinW = 140;

    // Ancho de los botones de transicion (las propias con su x detras) y de "+".
    const juce::Font font (DarkLookAndFeel::sans (12.0f, true));
    std::vector<int> widths;
    int transitionsW = 0;
    for (size_t i = 0; i < transitionButtons.size(); ++i)
    {
        widths.push_back (juce::GlyphArrangement::getStringWidthInt (font, transitionButtons[i]->getButtonText()) + 16);
        transitionsW += widths.back() + (removeButtons[i] != nullptr ? 22 : 0) + 4;
    }
    transitionsW += 28 + 8 + msButtonW;

    // Franja de abajo: una linea si cabe todo; si no, el crossfader arriba y la transicion debajo.
    const int stripInnerW = area.getWidth() - 24;
    const bool oneLine = ! showTransitions || stripInnerW >= 2 * 18 + crossMinW + 25 + transitionsW;
    if (! showTransitions)
        transitionsW = 0;
    cardArea = area.removeFromBottom (oneLine ? 46 : 82);
    area.removeFromBottom (gap);
    pageArea = area;

    {
        auto strip = cardArea.reduced (12, 6);
        juce::Rectangle<int> cross, row;
        separatorArea = {};
        if (! showTransitions)
            cross = strip;
        else if (oneLine)
        {
            row = strip.removeFromRight (transitionsW);
            separatorArea = strip.removeFromRight (25).withSizeKeepingCentre (1, 22);
            cross = strip;
        }
        else
        {
            cross = strip.removeFromTop (34);
            strip.removeFromTop (4);
            row = strip;
        }
        crossLabelsA = cross.removeFromLeft (18);
        crossLabelsB = cross.removeFromRight (18);
        centreLabelArea = cross.removeFromBottom (11);
        crossfader.setBounds (cross.reduced (4, 0));

        // La duracion a la derecha; los botones y "+" desde la izquierda.
        row = row.withSizeKeepingCentre (row.getWidth(), buttonH);
        transitionMs.setBounds (row.removeFromRight (msButtonW));
        transitionMs.setVisible (showTransitions && stage != nullptr);
        row.removeFromRight (8);
        int x = row.getX();
        for (size_t i = 0; i <= transitionButtons.size(); ++i)
        {
            const bool isAdd = i == transitionButtons.size();
            auto* b = isAdd ? static_cast<juce::Button*> (&addTransitionButton) : transitionButtons[i].get();
            auto* remove = isAdd ? nullptr : removeButtons[i].get();
            const int w = isAdd ? 28 : widths[i];
            const int total = w + (remove != nullptr ? 22 : 0);
            const bool fits = showTransitions && x + total <= row.getRight();
            b->setBounds (x, row.getY(), w, buttonH);
            b->setVisible (fits);
            if (remove != nullptr)
            {
                remove->setBounds (x + w, row.getY() + 4, 20, buttonH - 8);
                remove->setVisible (fits);
            }
            x += total + 4;
        }
    }

    // Tarjeta de arriba: el volumen de cada deck a los lados (titulo, fader con vumetro y valor).
    auto inner = pageArea.reduced (12, 10);
    for (size_t c = 0; c < channels.size(); ++c)
    {
        auto col = c == 0 ? inner.removeFromLeft (46) : inner.removeFromRight (46);
        faderTitleArea[c] = col.removeFromTop (14);
        col.removeFromTop (6);
        if (headphonesAvailable)
        {
            channels[c].pfl->setBounds (col.removeFromBottom (26).withSizeKeepingCentre (36, 26));
            col.removeFromBottom (4);
        }
        faderLabelArea[c] = col.removeFromBottom (14);
        col.removeFromBottom (6);
        auto meter = c == 0 ? col.removeFromRight (6) : col.removeFromLeft (6);
        channels[c].faderMeter.setBounds (meter);
        channels[c].volume.setBounds (col.reduced (2, 0));
    }
    inner.reduce (12, 0);

    // Pagina AUDIO: tira A | master | tira B.
    {
        auto page = inner;
        auto centre = page.withSizeKeepingCentre (juce::jmin (112, page.getWidth() / 4), page.getHeight());
        auto stripA = page.withRight (centre.getX() - 16);
        auto stripB = page.withLeft (centre.getRight() + 16);
        masterLabelArea = centre.removeFromTop (18);
        centre.removeFromTop (6);
        auto meters = centre.removeFromRight (22);
        masterLeft.setBounds (meters.removeFromLeft (9));
        meters.removeFromLeft (4);
        masterRight.setBounds (meters.removeFromLeft (9));
        if (headphonesAvailable)
        {
            // Master arriba y, debajo, los dos mandos de los audifonos lado a lado.
            const int knobH = juce::jmin (88, centre.getHeight() / 2);
            master.setBounds (centre.removeFromTop (knobH));
            centre.removeFromTop (4);
            auto row = centre.removeFromTop (juce::jmin (centre.getHeight(), 76));
            cueMixKnob.setBounds (row.removeFromLeft (row.getWidth() / 2));
            headphoneKnob.setBounds (row);
        }
        else
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

    // Pagina PROGRAMA: la vista previa 16:9, lo mas grande que quepa entre los volumenes.
    {
        const int h = juce::jmin (inner.getHeight(), inner.getWidth() * 9 / 16);
        previewArea = inner.withSizeKeepingCentre (h * 16 / 9, h);
        if (stage != nullptr)
            stage->getPreview().setBounds (previewArea);
        noVideoLabel.setBounds (previewArea);
    }
}
}
