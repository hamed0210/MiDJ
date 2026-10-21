#include "MainComponent.h"

namespace midj
{
MainComponent::MainComponent()
{
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);   // tambien las ventanas sueltas (dialogos, menus, ajustes)
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

    // YouTube: el reproductor (del tamano del programa), su lista guardada y la pestana del automix.
    youtubePlayer = std::make_unique<YouTubePlayer> (getDataFolder(), videoSettings().width, videoSettings().height);
    youtubeQueue.load (getDataFolder().getChildFile ("youtube.xml"));
    youtubePanel = std::make_unique<YouTubePanel> (*youtubePlayer, youtubeQueue);
    automixPanel.setYouTube (youtubePanel.get(), &youtubeQueue);
    youtubePlayer->onEnded = [this]
    {
        // Con el automix de YouTube, el siguiente; si no, se cierra (libera la memoria).
        if (youtubeQueue.isEnabled() && ! youtubeQueue.getItems().empty())
            youtubePanel->next();
        else
            juce::Timer::callAfterDelay (1500, [safe = juce::Component::SafePointer<MainComponent> (this)]
            {
                if (safe != nullptr && safe->youtubePlayer->getStatus().state == YouTubePlayer::State::ended)
                    safe->youtubePlayer->close();
            });
    };
    browser->onYouTubePlay = [this] (const YouTubeVideo& v)
    {
        youtubePanel->playNow (v);
        automixPanel.showYouTubeTab (true);
    };
    browser->onYouTubeQueue = [this] (const YouTubeVideo& v, bool next)
    {
        next ? youtubeQueue.insertNext (v) : youtubeQueue.add (v);
        automixPanel.showYouTubeTab (true);
        if (youtubeQueue.isEnabled() && ! youtubePlayer->isOpen())
            youtubePanel->next();
    };

    // Sampler: el banco se lee al arrancar y se guarda cada vez que cambia.
    mixer.getSampler().loadBank (getDataFolder().getChildFile ("sampler.xml"), formatManager);
    samplerPanel.onBankChanged = [this] { saveSamplerBank(); };
    samplerPanel.setCompact (true);   // en la barra de arriba
    addAndMakeVisible (samplerPanel);

    // Pestanas AUDIO / PROGRAMA del centro, en la barra de arriba junto a Proyector.
    for (auto* b : { &audioTabButton, &programTabButton })
    {
        b->setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (b);
    }
    audioTabButton.setTooltip ("EQ, filtro y vumetros de cada deck y el master (tecla V: cambiar de pestana)");
    programTabButton.setTooltip ("La salida de video (tecla V)");
    audioTabButton.onClick = [this] { mixerView.showVideoTab (false); };
    programTabButton.onClick = [this] { mixerView.showVideoTab (true); };
    mixerView.onTabChanged = [this] (bool program) { updateTabButtons (program); };
    updateTabButtons (mixerView.isVideoTabShowing());

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

    // Barritas de abajo: cambian el ancho del automix y de las escenas (la biblioteca, el resto).
    automixHandle.onStart = [this] { dragStartWidth = panels.automixWidth; };
    automixHandle.onDrag = [this] (int dx) { panels.automixWidth = juce::jlimit ((int) PanelSettings::minWidth, (int) PanelSettings::maxWidth, dragStartWidth - dx); resized(); };
    automixHandle.onReset = [this] { panels.automixWidth = PanelSettings::defaultAutomixWidth; resized(); };
    scenesHandle.onStart = [this] { dragStartWidth = panels.scenesWidth; };
    scenesHandle.onDrag = [this] (int dx) { panels.scenesWidth = juce::jlimit ((int) PanelSettings::minWidth, (int) PanelSettings::maxWidth, dragStartWidth - dx); resized(); };
    scenesHandle.onReset = [this] { panels.scenesWidth = PanelSettings::defaultScenesWidth; resized(); };
    addAndMakeVisible (automixHandle);
    addAndMakeVisible (scenesHandle);
    // Barrita entre Escenas y Fuentes: reparte el alto de la columna.
    scenesSplitHandle.onStart = [this] { dragStartShare = panels.scenesShare; };
    scenesSplitHandle.onDrag = [this] (int dy)
    {
        if (scenesColumn.getHeight() > 0)
            panels.scenesShare = juce::jlimit (0.15f, 0.85f, dragStartShare + (float) dy / (float) scenesColumn.getHeight());
        resized();
    };
    scenesSplitHandle.onReset = [this] { panels.scenesShare = PanelSettings::defaultScenesShare; resized(); };
    addAndMakeVisible (scenesSplitHandle);

    // Barra de arriba: proyector (menu de monitores), MIDI, atajos y ajustes.
    projectorButton.setTooltip ("Proyectar la salida de video a pantalla completa en un monitor");
    projectorButton.onClick = [this] { if (videoStage != nullptr) videoStage->showMenu(); };
    addAndMakeVisible (projectorButton);
    settingsButton.setBordered (true);
    settingsButton.onClick = [this] { showSettings(); };
    addAndMakeVisible (settingsButton);
    for (auto* view : { &deckViewA, &deckViewB })
        view->describe = [this] (const juce::File& file)
        {
            if (const auto t = library.findByFile (file); t && t->title.isNotEmpty())
                return std::pair<juce::String, juce::String> { t->title, t->artist };
            return std::pair<juce::String, juce::String> { file.getFileNameWithoutExtension(), juce::String() };
        };

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
    setSize (1366, 720);
    // Sin entradas; hasta 4 salidas (las 3 y 4, para los audifonos si la tarjeta las tiene),
    // con la tarjeta, frecuencia y bloque guardados.
    const auto savedDevice = loadAudioSettings();
    mixerView.limiterSettingChanged();
    mixerView.onLimiterChanged = [this] { saveAudioSettings(); };
    setAudioChannels (0, 4, savedDevice.get());
    setHeadphoneMode ((HeadphoneMode) headphoneMode.load(), headphoneDevice);
}

MainComponent::~MainComponent()
{
    saveLayout();
    saveAudioSettings();
    headphones.close();
    youtubeQueue.save (getDataFolder().getChildFile ("youtube.xml"));
    automixPanel.setYouTube (nullptr, nullptr);
    youtubePanel.reset();
    youtubePlayer.reset();
    removeKeyListener (this);
    if (keyWindow != nullptr)
        keyWindow->removeKeyListener (this);
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
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void MainComponent::parentHierarchyChanged()
{
    // Los atajos tambien cuando ningun control tiene el foco (JUCE manda entonces la tecla a la
    // ventana, que esta por encima de este componente).
    if (auto* top = getTopLevelComponent(); top != this && top != keyWindow.getComponent())
    {
        if (keyWindow != nullptr)
            keyWindow->removeKeyListener (this);
        keyWindow = top;
        top->addKeyListener (this);
    }
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
    return "Espacio\tPausa el deck (o los decks) que suena; otra vez: vuelve a poner ese mismo\n"
           "\t(si no sonaba ninguno: Play del deck activo)\n"
           "Tab\tCambiar de deck activo (el del borde de color; tambien con un clic en el deck)\n"
           "C / S / K\tCUE / SYNC / Keylock del deck activo\n"
           "Intro\tCargar la cancion elegida en el deck activo\n"
           "Flechas arriba / abajo\tMoverse por la lista de canciones\n"
           "Flechas izquierda / derecha\tCrossfader (con Mayus: del todo)\n"
           "A\tAgregar la cancion elegida al automix\n"
           "1 - 8\tPads del sampler\n"
           "Ctrl+1 - 9 (o F1 - F9)\tPasar a la escena 1 - 9 (en portatiles HP las F necesitan la tecla Fn)\n"
           "V\tPestana AUDIO / VIDEO del mezclador\n"
           "Ctrl+F\tBuscar en la biblioteca (Esc o Intro: salir del buscador)\n"
           "Supr\tQuitar la fuente elegida (vista previa o panel Fuentes)";
}

void MainComponent::spacePressed()
{
    // Lo que suena se pausa (y se recuerda); si no suena nada, vuelve lo que se pauso con el
    // espacio; si no hay nada recordado, el deck activo.
    // YouTube cuenta como uno mas: si suena (y ningun deck), se pausa y vuelve igual.
    std::array<DeckComponent*, 2> views { &deckViewA, &deckViewB };
    const std::array<bool, 2> playing { deckA.isPlaying(), deckB.isPlaying() };
    const bool youtubePlaying = youtubePlayer->getStatus().state == YouTubePlayer::State::playing;
    if (playing[0] || playing[1] || youtubePlaying)
    {
        for (size_t d = 0; d < 2; ++d)
        {
            pausedBySpace[d] = playing[d];
            if (playing[d])
                views[d]->togglePlay();
        }
        youtubePausedBySpace = youtubePlaying;
        if (youtubePlaying)
            youtubePlayer->pause();
        return;
    }
    if (std::exchange (youtubePausedBySpace, false) && youtubePlayer->getStatus().state == YouTubePlayer::State::paused)
    {
        youtubePlayer->play();
        pausedBySpace = {};
        return;
    }
    bool resumed = false;
    for (size_t d = 0; d < 2; ++d)
        if (pausedBySpace[d] && (d == 0 ? deckA : deckB).hasTrack())
        {
            views[d]->togglePlay();
            resumed = true;
        }
    pausedBySpace = {};
    if (! resumed)
        activeView().togglePlay();
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
        // Ctrl+1-9: escena 1-9 (como F1-F9, pero sin la tecla Fn de los portatiles).
        if (code >= '1' && code <= '9')
        {
            const auto& scenes = videoStage->getScenes().scenes;
            if (const int index = code - '1'; index < (int) scenes.size())
                videoStage->switchToScene (scenes[(size_t) index].id);
            return true;
        }
        return false;
    }
    if (mods.isAltDown())
        return false;

    if (code == juce::KeyPress::spaceKey)                 { spacePressed(); return true; }
    if (code == juce::KeyPress::tabKey)                   { setActiveDeck (1 - activeDeck); return true; }
    if (code == juce::KeyPress::upKey)                    { browser->moveSelection (-1); return true; }
    if (code == juce::KeyPress::downKey)                  { browser->moveSelection (+1); return true; }
    if (code == juce::KeyPress::pageUpKey)                { browser->moveSelection (-10); return true; }
    if (code == juce::KeyPress::pageDownKey)              { browser->moveSelection (+10); return true; }
    if (code == juce::KeyPress::returnKey)
    {
        if (const auto video = browser->getSelectedYouTube())
            browser->onYouTubePlay (*video);
        else if (const auto track = browser->getSelectedTrack())
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
            if (const auto video = browser->getSelectedYouTube())
                browser->onYouTubeQueue (*video, false);
            else if (const auto track = browser->getSelectedTrack())
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

void MainComponent::updateAutoCrossfader()
{
    // Al empezar a sonar un deck que el crossfader deja sin volumen (o casi), si el otro no
    // suena, el crossfader se desliza a su lado en ~0,4 s. Con el otro sonando no se toca: puede
    // ser una mezcla preparada. El automix maneja el crossfader solo.
    for (int d = 0; d < 2; ++d)
    {
        const bool playing = (d == 0 ? deckA : deckB).isPlaying();
        const bool otherPlaying = (d == 0 ? deckB : deckA).isPlaying();
        if (playing && ! wasPlaying[(size_t) d] && ! otherPlaying && panels.autoCrossfader && ! automix.isEnabled())
        {
            const float cross = mixer.getCrossfader();
            const float gain = juce::jmin (1.0f, 2.0f * (d == 0 ? 1.0f - cross : cross));
            if (gain < 0.99f)
                crossfaderTarget = d == 0 ? 0.0f : 1.0f;
        }
        wasPlaying[(size_t) d] = playing;
    }
    if (crossfaderTarget < 0.0f)
        return;
    if (mixerView.getCrossfader().isMouseButtonDown() || automix.isEnabled())
    {
        crossfaderTarget = -1.0f;   // la mano (o el automix) manda
        return;
    }
    const float cross = mixer.getCrossfader();
    const float step = 1.0f / 12.0f;   // 12 pasos a 30 por segundo
    const float next = std::abs (crossfaderTarget - cross) <= step ? crossfaderTarget : cross + (crossfaderTarget > cross ? step : -step);
    mixer.setCrossfader (next);
    if (next == crossfaderTarget)
        crossfaderTarget = -1.0f;
}

void MainComponent::updateAutoScene()
{
    // Lo que mas se oye: de los decks que suenan, el de mas volumen tras el crossfader.
    if (videoStage == nullptr)
        return;
    const float cross = mixer.getCrossfader();
    int kind = 0;
    float loudest = 0.0f;
    for (int d = 0; d < 2; ++d)
    {
        const auto& deck = d == 0 ? deckA : deckB;
        if (! deck.isPlaying())
            continue;
        const float level = mixer.getStrip (d).getVolume() * juce::jmin (1.0f, 2.0f * (d == 0 ? 1.0f - cross : cross));
        if (level > loudest)
        {
            loudest = level;
            kind = DeckVideo::isVideoFile (deck.getLoadedFile()) ? 2 : 1;
        }
    }
    // Sin nada sonando: 3. El cambio tiene que durar 0,6 s (un crossfader que pasa por el centro
    // no hace saltar la escena; 1,5 s para "no suena nada": una pausa corta tampoco) y solo actua
    // al cambiar: si despues se elige otra escena a mano, se respeta hasta el proximo cambio.
    // YouTube sonando (o su anuncio) manda sobre los decks: 4.
    if (youtubePlayer != nullptr && youtubePlayer->isSounding())
        kind = 4;
    if (kind == 0)
        kind = 3;
    const auto now = juce::Time::getMillisecondCounter();
    if (kind == autoKind)
    {
        pendingAutoKind = 0;
        return;
    }
    if (kind != pendingAutoKind)
    {
        pendingAutoKind = kind;
        pendingAutoSince = now;
        return;
    }
    if (now - pendingAutoSince < (kind == 3 ? 1500u : 600u))
        return;
    autoKind = kind;
    pendingAutoKind = 0;
    const auto wanted = kind == 4 ? SceneAuto::youtube : kind == 2 ? SceneAuto::video : kind == 1 ? SceneAuto::audio : SceneAuto::idle;
    for (const auto& scene : videoStage->getScenes().scenes)
        if (scene.autoWhen == wanted)
        {
            if (scene.id != videoStage->getScenes().currentScene)
                videoStage->switchToScene (scene.id);
            break;
        }
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
            playedSeconds[(size_t) d] = 0.0;
            playCounted[(size_t) d] = false;
            requestPoster (d, file);
            if (file != juce::File() && stems.isCached (file))
                stems.request (file, true);
        }
        // El video del deck sigue a su cancion (mientras carga otra, no se ve la anterior).
        (d == 0 ? videoA : videoB).setFile (deck.isLoading() ? juce::File() : file);
        // Mas reproducidos: cuenta una vez por carga, a los 30 s sonando.
        if (deck.isPlaying() && ! playCounted[(size_t) d] && file != juce::File())
        {
            playedSeconds[(size_t) d] += 1.0 / 30.0;
            if (playedSeconds[(size_t) d] >= 30.0)
            {
                playCounted[(size_t) d] = true;
                library.addPlay (file);
            }
        }
    }
    updateAutoCrossfader();
    if (timerTicks % 6 == 0)
        updateAutoScene();
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
    headphones.prepare (sampleRate);
    // El visualizador muestra lo que se oye: lo calculado hace la latencia de la tarjeta y un bloque.
    if (auto* device = deviceManager.getCurrentAudioDevice())
        mixer.getTap().setOutputLatency (device->getOutputLatencyInSamples() + samplesPerBlockExpected);
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    // El mezclador escribe el master en las salidas 1 y 2; despues, los audifonos donde toque.
    auto& buffer = *info.buffer;
    const int channels = buffer.getNumChannels(), n = info.numSamples;
    if (channels <= 0)
        return;
    float* masterChannels[2] = { buffer.getWritePointer (0, info.startSample),
                                 buffer.getWritePointer (juce::jmin (1, channels - 1), info.startSample) };
    juce::AudioBuffer<float> master (masterChannels, channels > 1 ? 2 : 1, n);
    mixer.getNextAudioBlock (juce::AudioSourceChannelInfo (&master, 0, n));
    for (int ch = 2; ch < channels; ++ch)
        buffer.clear (ch, info.startSample, n);

    const auto& cue = mixer.getCueBuffer();
    switch ((HeadphoneMode) headphoneMode.load())
    {
        case HeadphoneMode::channels34:
            if (channels >= 4)
            {
                buffer.copyFrom (2, info.startSample, cue, 0, 0, n);
                buffer.copyFrom (3, info.startSample, cue, 1, 0, n);
            }
            break;
        case HeadphoneMode::splitMono:
            if (channels >= 2)
            {
                // Izquierda: el master en mono; derecha: los audifonos en mono.
                float* l = masterChannels[0];
                float* r = masterChannels[1];
                const float* cl = cue.getReadPointer (0);
                const float* cr = cue.getReadPointer (1);
                for (int i = 0; i < n; ++i)
                {
                    l[i] = 0.5f * (l[i] + r[i]);
                    r[i] = 0.5f * (cl[i] + cr[i]);
                }
            }
            break;
        case HeadphoneMode::otherDevice:
            headphones.push (cue.getReadPointer (0), cue.getReadPointer (1), n);
            break;
        case HeadphoneMode::off:
            break;
    }
}

void MainComponent::setHeadphoneMode (HeadphoneMode mode, const juce::String& device)
{
    headphoneMode = (int) mode;
    headphoneDevice = device;
    if (mode == HeadphoneMode::otherDevice && device.isNotEmpty())
    {
        juce::String error;
        if (! headphones.open (device, error))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MiDJ",
                                                    "No se pudieron abrir los audifonos:\n" + error);
    }
    else
        headphones.close();
    mixerView.setHeadphonesAvailable (mode != HeadphoneMode::off);
}

void MainComponent::saveAudioSettings()
{
    juce::XmlElement xml ("AUDIO");
    xml.setAttribute ("headphones", headphoneMode.load());
    xml.setAttribute ("headphoneDevice", headphoneDevice);
    xml.setAttribute ("limiter", mixer.isLimiterEnabled());
    xml.setAttribute ("cueMix", (double) mixer.getCueMix());
    xml.setAttribute ("headphoneVolume", (double) mixer.getHeadphoneVolume());
    if (auto device = deviceManager.createStateXml())
        xml.addChildElement (device.release());
    xml.writeTo (getDataFolder().getChildFile ("audio.xml"));
}

std::unique_ptr<juce::XmlElement> MainComponent::loadAudioSettings()
{
    const auto xml = juce::XmlDocument::parse (getDataFolder().getChildFile ("audio.xml"));
    if (xml == nullptr)
        return nullptr;
    headphoneMode = juce::jlimit (0, 3, xml->getIntAttribute ("headphones", 0));
    headphoneDevice = xml->getStringAttribute ("headphoneDevice");
    mixer.setLimiterEnabled (xml->getBoolAttribute ("limiter", true));
    mixer.setCueMix ((float) xml->getDoubleAttribute ("cueMix", 0.0));
    mixer.setHeadphoneVolume ((float) xml->getDoubleAttribute ("headphoneVolume", Mixer::defaultMasterVolume));
    if (auto* device = xml->getChildByName ("DEVICESETUP"))
        return std::make_unique<juce::XmlElement> (*device);
    return nullptr;
}

void MainComponent::showAudioSettings()
{
    // Tarjeta de sonido (la de JUCE: tipo, salida, canales, frecuencia y bloque), audifonos y limitador.
    class Content : public juce::Component
    {
    public:
        explicit Content (MainComponent& o)
            : owner (o), selector (o.deviceManager, 0, 0, 2, 4, false, false, true, false)
        {
            addAndMakeVisible (selector);
            modeBox.addItem ("Sin audifonos", 1 + (int) HeadphoneMode::off);
            modeBox.addItem ("Salidas 3 y 4 de esta tarjeta (tarjetas de 4 canales)", 1 + (int) HeadphoneMode::channels34);
            modeBox.addItem ("Partir esta salida con un cable en Y (master y audifonos en mono)", 1 + (int) HeadphoneMode::splitMono);
            modeBox.addItem ("Otra tarjeta de sonido (p. ej. adaptador USB)", 1 + (int) HeadphoneMode::otherDevice);
            modeBox.setSelectedId (1 + owner.headphoneMode.load(), juce::dontSendNotification);
            modeBox.onChange = [this] { apply(); };
            addAndMakeVisible (modeBox);
            deviceBox.addItemList (HeadphoneOutput::outputNames(), 1);
            deviceBox.setText (owner.headphoneDevice, juce::dontSendNotification);
            deviceBox.setTextWhenNothingSelected ("Elige la tarjeta de los audifonos");
            deviceBox.onChange = [this] { apply(); };
            addAndMakeVisible (deviceBox);
            limiter.setButtonText ("Limitador del master (evita que la salida sature)");
            limiter.setToggleState (owner.mixer.isLimiterEnabled(), juce::dontSendNotification);
            limiter.onClick = [this] { owner.mixer.setLimiterEnabled (limiter.getToggleState()); owner.mixerView.limiterSettingChanged(); owner.saveAudioSettings(); };
            addAndMakeVisible (limiter);
            updateEnabled();
            setSize (560, 430);
        }

        ~Content() override { owner.saveAudioSettings(); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (DarkLookAndFeel::panel);
            g.setColour (DarkLookAndFeel::textQuiet);
            g.setFont (DarkLookAndFeel::sans (11.0f, true).withKerningFactor (0.08f));
            g.drawText ("AUDIFONOS (PRE-ESCUCHA)", 16, headphonesY, getWidth() - 32, 16, juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            selector.setBounds (r.removeFromTop (r.getHeight() - 150));
            headphonesY = r.getY() + 6;
            r.removeFromTop (26);
            r.reduce (8, 0);
            modeBox.setBounds (r.removeFromTop (30));
            r.removeFromTop (6);
            deviceBox.setBounds (r.removeFromTop (30));
            r.removeFromTop (10);
            limiter.setBounds (r.removeFromTop (30));
        }

    private:
        void apply()
        {
            const auto mode = (HeadphoneMode) (modeBox.getSelectedId() - 1);
            owner.setHeadphoneMode (mode, deviceBox.getText());
            owner.saveAudioSettings();
            updateEnabled();
        }
        void updateEnabled() { deviceBox.setEnabled (modeBox.getSelectedId() - 1 == (int) HeadphoneMode::otherDevice); }

        MainComponent& owner;
        juce::AudioDeviceSelectorComponent selector;
        juce::ComboBox modeBox, deviceBox;
        juce::ToggleButton limiter;
        int headphonesY = 0;
    };

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new Content (*this));
    options.dialogTitle = "Audio y audifonos";
    options.dialogBackgroundColour = DarkLookAndFeel::panel;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = this;
    options.launchAsync();
}

void MainComponent::releaseResources()
{
    mixer.releaseResources();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (DarkLookAndFeel::background);
    // Barra de arriba: nombre y formato de la salida de video; fondo de las pestanas.
    auto header = headerArea;
    g.setColour (DarkLookAndFeel::selection);
    g.fillRect (header.removeFromBottom (1));
    auto h = header.reduced (14, 0);
    g.setColour (DarkLookAndFeel::text);
    g.setFont (DarkLookAndFeel::sans (17.0f, true));
    g.drawText ("MiDJ", h.removeFromLeft (56), juce::Justification::centredLeft, false);
    if (videoStage != nullptr && videoStage->isReady())
    {
        g.setColour (DarkLookAndFeel::textQuiet);
        g.setFont (DarkLookAndFeel::mono (12.0f));
        g.drawText (juce::String (videoStage->getProgramWidth()) + juce::String::fromUTF8 (" \xc3\x97 ") + juce::String (videoStage->getProgramHeight())
                        + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (videoSettings().fps) + " fps",
                    h.removeFromLeft (170), juce::Justification::centredLeft, false);
    }
    g.setColour (DarkLookAndFeel::panel);
    g.fillRoundedRectangle (tabsArea.toFloat(), 8.0f);
}

void MainComponent::updateTabButtons (bool program)
{
    for (auto [b, on] : { std::pair<juce::TextButton*, bool> { &audioTabButton, ! program }, std::pair<juce::TextButton*, bool> { &programTabButton, program } })
    {
        b->setColour (juce::TextButton::buttonColourId, on ? DarkLookAndFeel::line : juce::Colours::transparentBlack);
        b->setColour (juce::TextButton::textColourOffId, on ? DarkLookAndFeel::text : DarkLookAndFeel::textQuiet);
    }
}

void MainComponent::panelsChanged()
{
    deckViewA.setSections (panels.deck);
    deckViewB.setSections (panels.deck);
    mixerView.setShowTransitions (panels.transitions);
    resized();
    repaint();
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    headerArea = area.removeFromTop (46);
    {
        // Derecha: AUDIO / PROGRAMA, Proyector, MIDI, Atajos y Ajustes. Izquierda: nombre, formato y sampler.
        auto h = headerArea.reduced (14, 8);
        settingsButton.setBounds (h.removeFromRight (32));
        h.removeFromRight (6);
        helpButton.setBounds (h.removeFromRight (70));
        h.removeFromRight (6);
        midiButton.setBounds (h.removeFromRight (58));
        h.removeFromRight (6);
        projectorButton.setBounds (h.removeFromRight (92));
        h.removeFromRight (8);
        tabsArea = h.removeFromRight (220);
        auto tabs = tabsArea.reduced (3);
        audioTabButton.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2));
        programTabButton.setBounds (tabs);
        h.removeFromLeft (56 + 170);
        // El sampler solo si cabe entero (en ventanas estrechas se oculta).
        const bool samplerFits = h.getWidth() >= SamplerPanel::compactWidth + 16;
        samplerPanel.setVisible (panels.sampler && samplerFits);
        if (samplerFits)
            samplerPanel.setBounds (h.removeFromLeft (SamplerPanel::compactWidth));
    }
    area.reduce (10, 8);
    constexpr int gap = 8;

    // Arriba los decks y el centro, con el alto que piden los decks (al ocultar partes de los
    // decks crece lo de abajo); abajo los paneles (si hay alguno a la vista).
    const bool anyBottom = panels.library || panels.automix || panels.scenes || panels.sources;
    const int deckH = juce::jmax (300, deckViewA.getPreferredHeight());
    auto top = anyBottom ? area.removeFromTop (juce::jlimit (240, juce::jmax (240, area.getHeight() - 160), deckH)) : area;
    if (anyBottom)
        area.removeFromTop (gap);

    const int deckWidth = juce::jlimit (330, 480, (top.getWidth() - 2 * gap) * 26 / 100);
    deckViewA.setBounds (top.removeFromLeft (deckWidth));
    deckViewB.setBounds (top.removeFromRight (deckWidth));
    top.reduce (gap, 0);
    mixerView.setBounds (top);

    // Abajo: biblioteca | automix | escenas y fuentes. La biblioteca se queda con lo que sobra.
    const bool column = panels.scenes || panels.sources;
    browser->setVisible (anyBottom && panels.library);
    automixPanel.setVisible (anyBottom && panels.automix);
    scenesPanel->setVisible (anyBottom && panels.scenes);
    sourcesPanel->setVisible (anyBottom && panels.sources);
    automixHandle.setVisible (anyBottom && panels.automix && panels.library);
    scenesHandle.setVisible (anyBottom && column && (panels.library || panels.automix));
    scenesSplitHandle.setVisible (anyBottom && panels.scenes && panels.sources);
    if (anyBottom)
    {
        auto bottom = area;
        constexpr int handle = 14;
        if (column)
        {
            // Escenas arriba y Fuentes abajo, con una barrita para repartir el alto; si solo se ve
            // una, ocupa la columna entera.
            scenesColumn = (panels.library || panels.automix) ? bottom.removeFromRight (panels.scenesWidth) : bottom;
            auto c = scenesColumn;
            if (panels.scenes && panels.sources)
            {
                scenesPanel->setBounds (c.removeFromTop (juce::roundToInt ((float) c.getHeight() * panels.scenesShare) - handle / 2));
                scenesSplitHandle.setBounds (c.removeFromTop (handle));
                sourcesPanel->setBounds (c);
            }
            else
                (panels.scenes ? static_cast<juce::Component&> (*scenesPanel) : *sourcesPanel).setBounds (c);
            if (panels.library || panels.automix)
                scenesHandle.setBounds (bottom.removeFromRight (handle));
        }
        if (panels.automix)
        {
            automixPanel.setBounds (panels.library ? bottom.removeFromRight (panels.automixWidth) : bottom);
            if (panels.library)
                automixHandle.setBounds (bottom.removeFromRight (handle));
        }
        if (panels.library)
            browser->setBounds (bottom);
    }

    midiOverlay.setBounds (getLocalBounds());
}

void MainComponent::showSettings()
{
    // Ventanita de Ajustes (junto al boton): paneles visibles, partes de los decks y anchos.
    class Content : public juce::Component
    {
    public:
        explicit Content (MainComponent& o) : owner (o)
        {
            auto& p = owner.getPanels();
            const std::array<std::pair<bool*, const char*>, numToggles> items { {
                { &p.library, "Biblioteca" }, { &p.automix, "Automix" }, { &p.scenes, "Escenas" }, { &p.sources, "Fuentes" },
                { &p.sampler, "Sampler (barra de arriba)" }, { &p.transitions, "Transiciones (junto al crossfader)" },
                { &p.autoCrossfader, "Crossfader automatico al dar Play" },
                { &p.deck.waveform, "Forma de onda" }, { &p.deck.pitch, "Pitch y KEY" }, { &p.deck.loops, "Loops" },
                { &p.deck.stems, "Stems" }, { &p.deck.effects, "Efectos (FX)" } } };
            for (size_t i = 0; i < items.size(); ++i)
            {
                auto& t = toggles[i];
                t.setButtonText (items[i].second);
                t.setToggleState (*items[i].first, juce::dontSendNotification);
                t.onClick = [this, flag = items[i].first, &t] { *flag = t.getToggleState(); owner.panelsChanged(); repaint(); };
                addAndMakeVisible (t);
            }
            reset.onClick = [this]
            {
                owner.getPanels() = {};
                for (auto& t : toggles)
                    t.setToggleState (true, juce::dontSendNotification);
                owner.panelsChanged();
                repaint();
            };
            addAndMakeVisible (reset);
            audioButton.onClick = [this] { owner.showAudioSettings(); };
            addAndMakeVisible (audioButton);
            setSize (320, widthsY + 58 + 34 + 8 + 34 + 14);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (DarkLookAndFeel::text);
            g.setFont (DarkLookAndFeel::sans (16.0f, true));
            g.drawText ("Ajustes", 16, 12, getWidth() - 32, 24, juce::Justification::centredLeft);
            g.setColour (DarkLookAndFeel::textQuiet);
            g.setFont (DarkLookAndFeel::sans (11.0f, true).withKerningFactor (0.08f));
            g.drawText ("PANELES Y OPCIONES", 16, 44, getWidth() - 32, 16, juce::Justification::centredLeft);
            g.drawText ("EN LOS DECKS", 16, 64 + numPanels * step + 6, getWidth() - 32, 16, juce::Justification::centredLeft);
            g.drawText ("ANCHOS", 16, widthsY, getWidth() - 32, 16, juce::Justification::centredLeft);
            const auto& p = owner.getPanels();
            const auto dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
            g.setFont (DarkLookAndFeel::sans (12.0f));
            g.setColour (DarkLookAndFeel::text);
            g.drawFittedText ("Automix " + juce::String (p.automixWidth) + " px" + dot + "Escenas y fuentes " + juce::String (p.scenesWidth)
                                  + " px" + dot + "Biblioteca: el resto",
                              16, widthsY + 18, getWidth() - 32, 32, juce::Justification::topLeft, 2);
        }

        void resized() override
        {
            for (int i = 0; i < numToggles; ++i)
            {
                const int y = 64 + i * step + (i >= numPanels ? 28 : 0);
                toggles[(size_t) i].setBounds (16, y, getWidth() - 32, step - 2);
            }
            reset.setBounds (16, widthsY + 58, 220, 34);
            audioButton.setBounds (16, widthsY + 58 + 42, 220, 34);
        }

    private:
        enum { numPanels = 7, numToggles = 12, step = 30, widthsY = 64 + numToggles * step + 28 + 8 };
        MainComponent& owner;
        std::array<juce::ToggleButton, numToggles> toggles;
        juce::TextButton reset { "Mostrar todo y restablecer anchos" }, audioButton { "Audio y audifonos..." };
    };

    juce::CallOutBox::launchAsynchronously (std::make_unique<Content> (*this), settingsButton.getScreenBounds(), nullptr);
}

void MainComponent::requestPoster (int deckIndex, const juce::File& file)
{
    auto& view = deckIndex == 0 ? deckViewA : deckViewB;
    if (file == juce::File() || ! DeckVideo::isVideoFile (file))
    {
        view.setPoster (file, {});
        return;
    }
    // Un fotograma a la quinta parte del video, pequeno, en un hilo aparte (no frena la interfaz).
    juce::Component::SafePointer<DeckComponent> target (&view);
    juce::Thread::launch ([target, file]
    {
        MediaFoundation::initialiseThisThread();
        juce::String error;
        juce::Image frame;
        if (auto decoder = VideoDecoder::open (file, 480, error, false))
            decoder->frameAt (juce::jmin (decoder->getDuration() * 0.2, 30.0), frame);
        juce::MessageManager::callAsync ([target, file, frame]
        {
            if (target != nullptr)
                target->setPoster (file, frame);
        });
    });
}

void MainComponent::saveLayout()
{
    juce::XmlElement xml ("LAYOUT");
    xml.setAttribute ("automix", panels.automixWidth);
    xml.setAttribute ("video", panels.scenesWidth);
    xml.setAttribute ("folders", browser->getFoldersWidth());
    xml.setAttribute ("tab", mixerView.isVideoTabShowing() ? "video" : "audio");
    xml.setAttribute ("showLibrary", panels.library);
    xml.setAttribute ("showAutomix", panels.automix);
    xml.setAttribute ("showScenes", panels.scenes);
    xml.setAttribute ("showSources", panels.sources);
    xml.setAttribute ("showTransitions", panels.transitions);
    xml.setAttribute ("autoCrossfader", panels.autoCrossfader);
    xml.setAttribute ("scenesShare", (double) panels.scenesShare);
    xml.setAttribute ("showSampler", panels.sampler);
    xml.setAttribute ("deckWaveform", panels.deck.waveform);
    xml.setAttribute ("deckPitch", panels.deck.pitch);
    xml.setAttribute ("deckLoops", panels.deck.loops);
    xml.setAttribute ("deckStems", panels.deck.stems);
    xml.setAttribute ("deckEffects", panels.deck.effects);
    xml.writeTo (getDataFolder().getChildFile ("layout.xml"));
}

void MainComponent::loadLayout()
{
    const auto xml = juce::XmlDocument::parse (getDataFolder().getChildFile ("layout.xml"));
    if (xml == nullptr)
        return;
    auto width = [&] (const char* name, int fallback)
    {
        const int w = xml->getIntAttribute (name);
        return w > 0 ? juce::jlimit ((int) PanelSettings::minWidth, (int) PanelSettings::maxWidth, w) : fallback;
    };
    panels.automixWidth = width ("automix", panels.automixWidth);
    panels.scenesWidth = width ("video", panels.scenesWidth);
    panels.library = xml->getBoolAttribute ("showLibrary", true);
    panels.automix = xml->getBoolAttribute ("showAutomix", true);
    panels.scenes = xml->getBoolAttribute ("showScenes", true);
    panels.sources = xml->getBoolAttribute ("showSources", panels.scenes);   // antes iban juntas
    panels.transitions = xml->getBoolAttribute ("showTransitions", true);
    panels.autoCrossfader = xml->getBoolAttribute ("autoCrossfader", true);
    panels.scenesShare = juce::jlimit (0.15f, 0.85f, (float) xml->getDoubleAttribute ("scenesShare", PanelSettings::defaultScenesShare));
    mixerView.setShowTransitions (panels.transitions);
    panels.sampler = xml->getBoolAttribute ("showSampler", true);
    panels.deck.waveform = xml->getBoolAttribute ("deckWaveform", true);
    panels.deck.pitch = xml->getBoolAttribute ("deckPitch", true);
    panels.deck.loops = xml->getBoolAttribute ("deckLoops", true);
    panels.deck.stems = xml->getBoolAttribute ("deckStems", true);
    panels.deck.effects = xml->getBoolAttribute ("deckEffects", true);
    deckViewA.setSections (panels.deck);
    deckViewB.setSections (panels.deck);
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
