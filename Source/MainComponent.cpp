#include "MainComponent.h"

namespace midj
{
MainComponent::MainComponent()
{
    setLookAndFeel (&lookAndFeel);
    formatManager.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg y MP3

    // Biblioteca: lo que un deck analiza se guarda para la proxima vez.
    if (! library.open())
        DBG ("No se pudo abrir la biblioteca: " << Library::getDefaultFile().getFullPathName());
    for (auto* deck : { &deckA, &deckB })
        deck->onAnalysed = [this] (const juce::File& file, const TempoAnalysis& analysis) { library.setAnalysis (file, analysis); };
    deckViewA.onLoadFile = [this] (const juce::File& file) { loadIntoDeck (deckA, file); };
    deckViewB.onLoadFile = [this] (const juce::File& file) { loadIntoDeck (deckB, file); };

    browser = std::make_unique<LibraryBrowser> (library);
    browser->onLoadRequest = [this] (const TrackInfo& track) { loadIntoFreeDeck (track.getFile()); };
    browser->onLoadToDeck = [this] (const TrackInfo& track, int deckIndex) { loadIntoDeck (deckIndex == 0 ? deckA : deckB, track.getFile()); };

    // Automix: la lista se guarda al cerrar y se recupera al abrir.
    automix.loadTrack = [this] (int deckIndex, const juce::File& file) { return loadIntoDeck (deckIndex == 0 ? deckA : deckB, file); };
    automix.loadQueue (getDataFolder().getChildFile ("automix.xml"));
    browser->setAutomix (&automix);
    controlSurface.automix = &automix;
    library.startBackgroundWork();   // re-escanea las carpetas por si hubo cambios

    addAndMakeVisible (deckViewA);
    addAndMakeVisible (deckViewB);
    addAndMakeVisible (mixerView);
    addAndMakeVisible (*browser);

    // Sampler: el banco se lee al arrancar y se guarda cada vez que cambia.
    mixer.getSampler().loadBank (getDataFolder().getChildFile ("sampler.xml"), formatManager);
    samplerPanel.onBankChanged = [this] { saveSamplerBank(); };
    addAndMakeVisible (samplerPanel);

    crossfader.setSliderStyle (juce::Slider::LinearHorizontal);
    crossfader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    crossfader.setRange (0.0, 1.0);
    crossfader.setValue (0.5, juce::dontSendNotification);
    crossfader.setDoubleClickReturnValue (true, 0.5);
    crossfader.onValueChange = [this] { mixer.setCrossfader ((float) crossfader.getValue()); };
    addAndMakeVisible (crossfader);

    crossfaderLabel.setText ("CROSSFADER", juce::dontSendNotification);
    crossfaderLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (crossfaderLabel);

    // Controladoras MIDI: mapeo guardado, dispositivos y ganchos hacia la interfaz.
    controlSurface.onBrowserScroll = [this] (int steps) { browser->moveSelection (steps); };
    controlSurface.onLoadSelected = [this] (int deckIndex)
    {
        if (const auto track = browser->getSelectedTrack())
            loadIntoDeck (deckIndex == 0 ? deckA : deckB, track->getFile());
    };
    midi.loadMapping();
    midi.startDevices();
    midiButton.setClickingTogglesState (true);
    midiButton.setTooltip ("Aprender MIDI: asignar los controles de una controladora");
    midiButton.onClick = [this] { toggleMidiLearn(); };
    addAndMakeVisible (midiButton);
    addChildComponent (midiOverlay);
    startTimerHz (30);   // automix (decisiones y correccion de fase) y crossfader en pantalla

    const auto args = juce::JUCEApplicationBase::getCommandLineParameterArray();
    if (const int i = args.indexOf ("--simular-midi"); i >= 0 && i + 1 < args.size())
        startMidiSimulation (juce::File (args[i + 1].unquoted()));

    setSize (1316, 665);
    setAudioChannels (0, 2);   // sin entradas, salida estereo
}

MainComponent::~MainComponent()
{
    shutdownAudio();
    stopTimer();
    saveSamplerBank();
    automix.saveQueue (getDataFolder().getChildFile ("automix.xml"));
    browser->setAutomix (nullptr);
    browser.reset();
    library.close();
    setLookAndFeel (nullptr);
}

bool MainComponent::loadIntoDeck (Deck& deck, const juce::File& file)
{
    TempoAnalysis known;
    if (const auto track = library.findByFile (file); track && track->analysed)
        known = { track->bpm, track->firstBeat };
    if (deck.load (file, known))
        return true;
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MiDJ",
                                            "No se pudo abrir el archivo:\n" + file.getFullPathName());
    return false;
}

void MainComponent::loadIntoFreeDeck (const juce::File& file)
{
    if (! deckA.isPlaying())
        loadIntoDeck (deckA, file);
    else if (! deckB.isPlaying())
        loadIntoDeck (deckB, file);
    else
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "MiDJ",
                                                "Los dos decks estan sonando. Arrastra la cancion al deck que quieras cambiar.");
}

void MainComponent::toggleMidiLearn()
{
    const bool on = midiButton.getToggleState();
    if (on)
    {
        std::vector<LearnTarget> targets;
        deckViewA.addLearnTargets (targets, "deck1.");
        deckViewB.addLearnTargets (targets, "deck2.");
        mixerView.addLearnTargets (targets);
        samplerPanel.addLearnTargets (targets);
        targets.push_back ({ &crossfader, "crossfader" });
        browser->addLearnTargets (targets);
        midiOverlay.setTargets (std::move (targets));
    }
    midiOverlay.setVisible (on);
    midiButton.toFront (false);   // el boton sigue accesible encima de la capa
    midiButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffa726).withAlpha (0.8f));
}

void MainComponent::saveSamplerBank()
{
    mixer.getSampler().saveBank (getDataFolder().getChildFile ("sampler.xml"), getDataFolder().getChildFile ("Grabaciones"));
}

void MainComponent::timerCallback()
{
    automix.update();
    if (++timerTicks % 8 == 0)
        browser->updateAutomixStatus();
    if (! crossfader.isMouseButtonDown())
        crossfader.setValue (mixer.getCrossfader(), juce::dontSendNotification);
}

void MainComponent::startMidiSimulation (const juce::File& script)
{
    // Cada linea: milisegundos desde el arranque, tipo (note/cc/pb), canal, numero, valor.
    // Los mensajes entran por el mismo camino que los de una controladora real.
    juce::StringArray lines;
    script.readLines (lines);
    for (auto line : lines)
    {
        line = line.upToFirstOccurrenceOf ("#", false, false).trim();
        juce::StringArray f;
        f.addTokens (line, " \t", "");
        f.removeEmptyStrings();
        if (f.size() < 5)
            continue;
        const int at = f[0].getIntValue(), channel = juce::jlimit (1, 16, f[2].getIntValue());
        const int number = f[3].getIntValue(), value = f[4].getIntValue();
        juce::MidiMessage m;
        if (f[1] == "note")      m = value > 0 ? juce::MidiMessage::noteOn (channel, number, (juce::uint8) value) : juce::MidiMessage::noteOff (channel, number);
        else if (f[1] == "cc")   m = juce::MidiMessage::controllerEvent (channel, number, value);
        else if (f[1] == "pb")   m = juce::MidiMessage::pitchWheel (channel, value);
        else continue;
        juce::Timer::callAfterDelay (at, [this, m] { midi.handleMessage (m); });
    }
}

void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    mixer.prepareToPlay (samplesPerBlockExpected, sampleRate);
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    mixer.getNextAudioBlock (info);
}

void MainComponent::releaseResources()
{
    mixer.releaseResources();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (DarkLookAndFeel::background);
}

void MainComponent::resized()
{
    // Arriba los decks y el mezclador, en medio el crossfader y abajo el navegador.
    auto area = getLocalBounds().reduced (6);
    auto top = area.removeFromTop (juce::jmax (330, area.getHeight() * 52 / 100));
    auto fader = area.removeFromTop (46).withSizeKeepingCentre (360, 46);
    samplerPanel.setBounds (area.removeFromRight (330).reduced (6));
    browser->setBounds (area.reduced (6));

    const auto centre = top.withSizeKeepingCentre (250, top.getHeight());
    mixerView.setBounds (centre.reduced (6));
    deckViewA.setBounds (top.withRight (centre.getX()).reduced (6));
    deckViewB.setBounds (top.withLeft (centre.getRight()).reduced (6));

    midiButton.setBounds (fader.getRight() + 24, fader.getY() + 10, 64, 28);
    midiOverlay.setBounds (getLocalBounds());
    crossfaderLabel.setBounds (fader.removeFromTop (20));
    crossfader.setBounds (fader.reduced (0, 2));
}
}
