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
    library.startBackgroundWork();   // re-escanea las carpetas por si hubo cambios

    addAndMakeVisible (deckViewA);
    addAndMakeVisible (deckViewB);
    addAndMakeVisible (mixerView);
    addAndMakeVisible (*browser);

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

    setSize (1316, 665);
    setAudioChannels (0, 2);   // sin entradas, salida estereo
}

MainComponent::~MainComponent()
{
    shutdownAudio();
    browser.reset();
    library.close();
    setLookAndFeel (nullptr);
}

void MainComponent::loadIntoDeck (Deck& deck, const juce::File& file)
{
    TempoAnalysis known;
    if (const auto track = library.findByFile (file); track && track->analysed)
        known = { track->bpm, track->firstBeat };
    if (! deck.load (file, known))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MiDJ",
                                                "No se pudo abrir el archivo:\n" + file.getFullPathName());
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
    browser->setBounds (area.reduced (6));

    const auto centre = top.withSizeKeepingCentre (250, top.getHeight());
    mixerView.setBounds (centre.reduced (6));
    deckViewA.setBounds (top.withRight (centre.getX()).reduced (6));
    deckViewB.setBounds (top.withLeft (centre.getRight()).reduced (6));

    crossfaderLabel.setBounds (fader.removeFromTop (20));
    crossfader.setBounds (fader.reduced (0, 2));
}
}
