#include "MainComponent.h"

namespace midj
{
MainComponent::MainComponent()
{
    setLookAndFeel (&lookAndFeel);
    formatManager.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg y MP3
    formatManager.registerFormat (new MediaFoundationAudioFormat(), false);   // el sonido de MP4/MOV/WMV y M4A

    // Biblioteca: lo que un deck analiza se guarda para la proxima vez.
    if (! library.open())
    {
        DBG ("No se pudo abrir la biblioteca: " << Library::getDefaultFile().getFullPathName());
    }
    for (auto* deck : { &deckA, &deckB })
        deck->onAnalysed = [this] (const juce::File& file, const TempoAnalysis& analysis) { library.setAnalysis (file, analysis); };
    deckViewA.onLoadFile = [this] (const juce::File& file) { loadIntoDeck (deckA, file); };
    deckViewB.onLoadFile = [this] (const juce::File& file) { loadIntoDeck (deckB, file); };
    deckViewA.onActivated = [this] { setActiveDeck (0); };
    deckViewB.onActivated = [this] { setActiveDeck (1); };
    deckViewA.setActive (true);

    browser = std::make_unique<LibraryBrowser> (library);
    browser->onLoadRequest = [this] (const TrackInfo& track) { loadIntoFreeDeck (track.getFile()); };
    browser->onReturnKey = [this] (const TrackInfo& track) { loadIntoDeck (activeDeck == 0 ? deckA : deckB, track.getFile()); };
    browser->onLoadToDeck = [this] (const TrackInfo& track, int deckIndex)
    {
        if (loadIntoDeck (deckIndex == 0 ? deckA : deckB, track.getFile()))
            setActiveDeck (deckIndex);
    };

    // Automix: la lista se guarda al cerrar y se recupera al abrir.
    automix.loadTrack = [this] (int deckIndex, const juce::File& file) { return loadIntoDeck (deckIndex == 0 ? deckA : deckB, file); };
    automix.loadQueue (getDataFolder().getChildFile ("automix.xml"));
    browser->setAutomix (&automix);

    // Stems: la separacion va en segundo plano; cuando esta, la voz va al deck que tenga esa cancion.
    stems.onReady = [this] (const juce::File& song, std::shared_ptr<const juce::AudioBuffer<float>> vocals)
    {
        for (auto* deck : { &deckA, &deckB })
            deck->setVocals (song, vocals);
    };
    stems.onFailed = [] (const juce::File& song, const juce::String& error)
    {
        if (error != "cancelado")
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MiDJ",
                                                    "No se pudieron separar los stems de " + song.getFileName() + ":\n" + error);
    };
    browser->onPrepareStems = [this] (const TrackInfo& track) { stems.request (track.getFile(), false); };
    for (auto [view, deck] : { std::pair<DeckComponent*, Deck*> { &deckViewA, &deckA }, std::pair<DeckComponent*, Deck*> { &deckViewB, &deckB } })
    {
        view->onStemsRequested = [this, deck] { if (deck->getLoadedFile() != juce::File()) stems.request (deck->getLoadedFile(), true); };
        view->getStemsState = [this, deck]
        {
            using S = DeckComponent::StemsState;
            const auto file = deck->getLoadedFile();
            switch (stems.getStatus (file))
            {
                case StemsService::Status::queued:  return std::pair<S, double> { S::queued, 0.0 };
                case StemsService::Status::working: return std::pair<S, double> { S::working, stems.getProgress (file) };
                case StemsService::Status::failed:  return std::pair<S, double> { S::failed, 0.0 };
                default:                            return std::pair<S, double> { S::none, 0.0 };
            }
        };
    }
    controlSurface.automix = &automix;
    library.startBackgroundWork();   // re-escanea las carpetas por si hubo cambios

    addAndMakeVisible (deckViewA);
    addAndMakeVisible (deckViewB);
    addAndMakeVisible (mixerView);
    addAndMakeVisible (*browser);
    addAndMakeVisible (automixPanel);

    // Sampler: el banco se lee al arrancar y se guarda cada vez que cambia.
    mixer.getSampler().loadBank (getDataFolder().getChildFile ("sampler.xml"), formatManager);
    samplerPanel.onBankChanged = [this] { saveSamplerBank(); };
    addAndMakeVisible (samplerPanel);

    // Video: el escenario existe siempre (las escenas y fuentes estan en la ventana principal);
    // solo compone mientras se ve la pestana VIDEO o hay un proyector.
    videoStage = std::make_unique<VideoStage> (std::array<DeckVideo*, 2> { &videoA, &videoB },
                                               std::array<const Deck*, 2> { &deckA, &deckB }, mixer, formatManager, videoSettings(),
                                               getDataFolder().getChildFile ("scenes.xml"));
    videoStage->getTitle = [this] (int d) { return videoTitleFor (d); };
    mixerView.setVideoStage (videoStage.get());
    scenesPanel = std::make_unique<ScenesPanel> (*videoStage);
    sourcesPanel = std::make_unique<SourcesPanel> (*videoStage);
    addAndMakeVisible (*scenesPanel);
    addAndMakeVisible (*sourcesPanel);

    // Divisores de abajo: biblioteca | automix | escenas y fuentes.
    bottomLayout.setItemLayout (0, 380, -1.0, -0.56);
    bottomLayout.setItemLayout (1, 8, 8, 8);
    bottomLayout.setItemLayout (2, 170, 600, 260);
    bottomLayout.setItemLayout (3, 8, 8, 8);
    bottomLayout.setItemLayout (4, 190, 700, 280);
    for (auto* bar : { &bottomBar1, &bottomBar2 })
    {
        bar->setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        addAndMakeVisible (bar);
    }

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
    helpButton.setTooltip ("Atajos de teclado");
    helpButton.onClick = [this]
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Atajos de teclado", shortcutsHelp(), {}, this);
    };
    addAndMakeVisible (helpButton);
    addChildComponent (midiOverlay);
    startTimerHz (30);   // automix (decisiones y correccion de fase) y estado en pantalla

    const auto args = juce::JUCEApplicationBase::getCommandLineParameterArray();
    if (const int i = args.indexOf ("--simular-midi"); i >= 0 && i + 1 < args.size())
        startMidiSimulation (juce::File (args[i + 1].unquoted()));

    // Teclado: los atajos llegan aqui desde cualquier control que no los use.
    setWantsKeyboardFocus (true);
    addKeyListener (this);
    keepFocusForShortcuts (*this);

    loadLayout();
    setSize (1316, 700);
    setAudioChannels (0, 2);   // sin entradas, salida estereo
}

MainComponent::~MainComponent()
{
    saveLayout();
    removeKeyListener (this);
    mixerView.setVideoStage (nullptr);
    scenesPanel.reset();
    sourcesPanel.reset();
    videoStage.reset();
    shutdownAudio();
    stopTimer();
    saveSamplerBank();
    automix.saveQueue (getDataFolder().getChildFile ("automix.xml"));
    browser->setAutomix (nullptr);
    browser.reset();
    library.close();
    setLookAndFeel (nullptr);
}

void MainComponent::parentHierarchyChanged()
{
    // Con la ventana ya creada, el foco del teclado empieza aqui (para los atajos).
    juce::Component::SafePointer<MainComponent> self (this);
    juce::MessageManager::callAsync ([self] { if (self != nullptr && self->isShowing()) self->grabKeyboardFocus(); });
}

void MainComponent::keepFocusForShortcuts (juce::Component& root)
{
    for (auto* c : root.getChildren())
    {
        const bool keepsFocus = dynamic_cast<juce::TextEditor*> (c) != nullptr || dynamic_cast<juce::ListBox*> (c) != nullptr
                             || dynamic_cast<ProjectorView*> (c) != nullptr;
        if (! keepsFocus)
        {
            c->setMouseClickGrabsKeyboardFocus (false);
            c->setWantsKeyboardFocus (false);
        }
        if (dynamic_cast<juce::ListBox*> (c) == nullptr)   // dentro de las listas JUCE gestiona su foco
            keepFocusForShortcuts (*c);
    }
}

juce::String MainComponent::shortcutsHelp()
{
    return "Espacio\tPlay / Pausa del deck activo\n"
           "Tab\tCambiar de deck activo (el del borde de color; tambien con un clic en el deck)\n"
           "C / S / K\tCUE / SYNC / Keylock del deck activo\n"
           "Intro\tCargar la cancion elegida en el deck activo\n"
           "Flechas arriba / abajo\tMoverse por la lista de canciones\n"
           "Flechas izquierda / derecha\tCrossfader (con Mayus: del todo)\n"
           "A\tAgregar la cancion elegida al automix\n"
           "1 - 8\tPads del sampler\n"
           "F1 - F9\tPasar a la escena 1 - 9\n"
           "V\tPestana AUDIO / VIDEO del mezclador\n"
           "Ctrl+F\tBuscar en la biblioteca (Esc o Intro: salir del buscador)\n"
           "Supr\tQuitar la fuente elegida (vista previa o panel Fuentes)";
}

void MainComponent::setActiveDeck (int deckIndex)
{
    activeDeck = deckIndex;
    deckViewA.setActive (deckIndex == 0);
    deckViewB.setActive (deckIndex == 1);
}

bool MainComponent::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    if (mods.isCommandDown())
    {
        if (code == 'F')
        {
            browser->focusSearch();
            return true;
        }
        return false;
    }
    if (mods.isAltDown())
        return false;

    if (code == juce::KeyPress::spaceKey)                 { activeView().togglePlay(); return true; }
    if (code == juce::KeyPress::tabKey)                   { setActiveDeck (1 - activeDeck); return true; }
    if (code == juce::KeyPress::upKey)                    { browser->moveSelection (-1); return true; }
    if (code == juce::KeyPress::downKey)                  { browser->moveSelection (+1); return true; }
    if (code == juce::KeyPress::pageUpKey)                { browser->moveSelection (-10); return true; }
    if (code == juce::KeyPress::pageDownKey)              { browser->moveSelection (+10); return true; }
    if (code == juce::KeyPress::returnKey)
    {
        if (const auto track = browser->getSelectedTrack())
            loadIntoDeck (activeDeck == 0 ? deckA : deckB, track->getFile());
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        const float step = code == juce::KeyPress::leftKey ? -1.0f : 1.0f;
        mixer.setCrossfader (mods.isShiftDown() ? (step < 0.0f ? 0.0f : 1.0f) : mixer.getCrossfader() + step * 0.05f);
        return true;
    }
    if (code >= juce::KeyPress::F1Key && code <= juce::KeyPress::F9Key)
    {
        const int index = code - juce::KeyPress::F1Key;
        const auto& scenes = videoStage->getScenes().scenes;
        if (index < (int) scenes.size())
            videoStage->switchToScene (scenes[(size_t) index].id);
        return true;
    }
    const auto ch = juce::CharacterFunctions::toUpperCase (key.getTextCharacter());
    if (ch >= '1' && ch <= '8')
    {
        const int pad = (int) (ch - '1');
        if (! padKeyDown[(size_t) pad])   // la repeticion del teclado no vuelve a dispararlo
        {
            padKeyDown[(size_t) pad] = true;
            mixer.getSampler().press (pad);
        }
        return true;
    }
    switch (ch)
    {
        case 'C': activeView().pressCue(); return true;
        case 'S': activeView().pressSync(); return true;
        case 'K': activeView().toggleKeyLock(); return true;
        case 'V': mixerView.showVideoTab (! mixerView.isVideoTabShowing()); return true;
        case 'A':
            if (const auto track = browser->getSelectedTrack())
                automix.add ({ track->getFile(), track->title, track->artist, track->bpm });
            return true;
        default: break;
    }
    return false;
}

bool MainComponent::keyStateChanged (bool, juce::Component*)
{
    // Soltar la tecla de un pad lo suelta (en los pads de modo mantener deja de sonar).
    bool any = false;
    for (int pad = 0; pad < Sampler::numPads; ++pad)
        if (padKeyDown[(size_t) pad] && ! juce::KeyPress::isKeyCurrentlyDown ('1' + pad))
        {
            padKeyDown[(size_t) pad] = false;
            mixer.getSampler().release (pad);
            any = true;
        }
    return any;
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
    // El deck donde se carga queda activo: el espacio la pone a sonar.
    if (! deckA.isPlaying())
    {
        if (loadIntoDeck (deckA, file))
            setActiveDeck (0);
    }
    else if (! deckB.isPlaying())
    {
        if (loadIntoDeck (deckB, file))
            setActiveDeck (1);
    }
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
        browser->addLearnTargets (targets);
        automixPanel.addLearnTargets (targets);
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
    // Cancion nueva en un deck con stems ya separados: se leen de la cache (tarda menos de un segundo).
    for (int d = 0; d < 2; ++d)
    {
        auto& deck = d == 0 ? deckA : deckB;
        const auto file = deck.getLoadedFile();
        if (file != lastLoaded[(size_t) d] && ! deck.isLoading())
        {
            lastLoaded[(size_t) d] = file;
            if (file != juce::File() && stems.isCached (file))
                stems.request (file, true);
        }
        // El video del deck sigue a su cancion (mientras carga otra, no se ve la anterior).
        (d == 0 ? videoA : videoB).setFile (deck.isLoading() ? juce::File() : file);
    }
    if (++timerTicks % 8 == 0)
        automixPanel.updateStatus();
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
    // El visualizador muestra lo que se oye: lo calculado hace la latencia de la tarjeta y un bloque.
    if (auto* device = deviceManager.getCurrentAudioDevice())
        mixer.getTap().setOutputLatency (device->getOutputLatencyInSamples() + samplesPerBlockExpected);
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
    // Arriba los decks y el mezclador, en medio el sampler y abajo los paneles.
    auto area = getLocalBounds().reduced (6);
    auto top = area.removeFromTop (juce::jmax (340, area.getHeight() * 50 / 100));
    auto middle = area.removeFromTop (76);

    const auto centre = top.withSizeKeepingCentre (270, top.getHeight());
    mixerView.setBounds (centre.reduced (6));
    deckViewA.setBounds (top.withRight (centre.getX()).reduced (6));
    deckViewB.setBounds (top.withLeft (centre.getRight()).reduced (6));

    // El sampler donde estaba el crossfader: bajo el deck A y el mezclador.
    samplerPanel.setBounds (middle.withRight (centre.getRight()).reduced (6, 2));
    auto buttons = middle.withLeft (centre.getRight() + 6).withSizeKeepingCentre (middle.getWidth() - centre.getRight() - 12, 30);
    midiButton.setBounds (buttons.removeFromLeft (64));
    buttons.removeFromLeft (8);
    helpButton.setBounds (buttons.removeFromLeft (72));

    // Abajo: biblioteca | automix | escenas y fuentes (divisores arrastrables).
    auto bottom = area.reduced (6, 4);
    juce::Component* columns[] = { browser.get(), &bottomBar1, &automixPanel, &bottomBar2, &videoColumn };
    bottomLayout.layOutComponents (columns, 5, bottom.getX(), bottom.getY(), bottom.getWidth(), bottom.getHeight(), false, true);
    auto video = videoColumn.getBounds();
    scenesPanel->setBounds (video.removeFromTop (video.getHeight() * 45 / 100));
    video.removeFromTop (6);
    sourcesPanel->setBounds (video);

    midiOverlay.setBounds (getLocalBounds());
}

void MainComponent::saveLayout()
{
    juce::XmlElement xml ("LAYOUT");
    xml.setAttribute ("library", (int) bottomLayout.getItemCurrentAbsoluteSize (0));
    xml.setAttribute ("automix", (int) bottomLayout.getItemCurrentAbsoluteSize (2));
    xml.setAttribute ("video", (int) bottomLayout.getItemCurrentAbsoluteSize (4));
    xml.setAttribute ("folders", browser->getFoldersWidth());
    xml.setAttribute ("tab", mixerView.isVideoTabShowing() ? "video" : "audio");
    xml.writeTo (getDataFolder().getChildFile ("layout.xml"));
}

void MainComponent::loadLayout()
{
    const auto xml = juce::XmlDocument::parse (getDataFolder().getChildFile ("layout.xml"));
    if (xml == nullptr)
        return;
    // Los anchos guardados como preferidos (si la ventana es mas estrecha, se reparten).
    if (const int w = xml->getIntAttribute ("library"); w > 0)
        bottomLayout.setItemLayout (0, 380, -1.0, w);
    if (const int w = xml->getIntAttribute ("automix"); w > 0)
        bottomLayout.setItemLayout (2, 170, 600, w);
    if (const int w = xml->getIntAttribute ("video"); w > 0)
        bottomLayout.setItemLayout (4, 190, 700, w);
    if (const int w = xml->getIntAttribute ("folders"); w > 0)
        browser->setFoldersWidth (w);
    if (xml->getStringAttribute ("tab") == "video")
        mixerView.showVideoTab (true);
}

juce::String MainComponent::videoTitleFor (int deckIndex)
{
    const auto& deck = deckIndex == 0 ? deckA : deckB;
    const auto file = deck.isLoading() ? juce::File() : deck.getLoadedFile();
    if (file == juce::File())
        return {};
    auto& cached = videoTitles[(size_t) deckIndex];
    if (cached.first != file)
    {
        // "Artista - Titulo" de la biblioteca; si no esta, el nombre del archivo.
        cached = { file, file.getFileNameWithoutExtension() };
        if (const auto track = library.findByFile (file); track && track->title.isNotEmpty())
            cached.second = track->artist.isNotEmpty() ? track->artist + " - " + track->title : track->title;
    }
    return cached.second;
}

bool MainComponent::videoGpuAllowed()
{
    const auto xml = juce::XmlDocument::parse (getDataFolder().getChildFile ("video.xml"));
    return xml == nullptr || xml->getStringAttribute ("device") != "cpu";
}

Compositor::Settings MainComponent::videoSettings()
{
    Compositor::Settings s;
    if (const auto xml = juce::XmlDocument::parse (getDataFolder().getChildFile ("video.xml")))
    {
        s.width = xml->getIntAttribute ("width", s.width);
        s.height = xml->getIntAttribute ("height", s.height);
        s.fps = xml->getIntAttribute ("fps", s.fps);
    }
    return s;
}
}
