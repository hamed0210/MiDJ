#include "UI/DeckComponent.h"
#include "UI/DarkLookAndFeel.h"

namespace midj
{
namespace
{
    juce::String formatTime (double seconds)
    {
        const int total = (int) std::max (0.0, seconds);
        return juce::String::formatted ("%d:%02d", total / 60, total % 60);
    }
}

DeckComponent::DeckComponent (Deck& d, const juce::String& name)
    : deck (d), deckName (name)
{
    deck.addChangeListener (this);
    deck.getThumbnail().addChangeListener (this);

    loadButton.onClick = [this] { chooseFile(); };
    playButton.onClick = [this]
    {
        deck.isPlaying() ? deck.pause() : deck.play();
        updateLabels();
    };
    addAndMakeVisible (loadButton);
    addAndMakeVisible (playButton);

    // Tempo +-8 %, como el fader de pitch de un plato.
    tempoSlider.setSliderStyle (juce::Slider::LinearVertical);
    tempoSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    tempoSlider.setRange (-8.0, 8.0, 0.01);
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
                                                   "*.mp3;*.wav;*.flac;*.ogg;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file.existsAsFile() && ! deck.load (file))
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                          "MiDJ", "No se pudo abrir el archivo.");
                          });
}

void DeckComponent::updateLabels()
{
    const auto file = deck.getFile();
    titleLabel.setText (deckName + "   " + (file == juce::File() ? juce::String ("Sin cancion") : file.getFileNameWithoutExtension()),
                        juce::dontSendNotification);

    const double bpm = deck.getCurrentBpm();
    bpmLabel.setText (bpm > 0.0 ? juce::String (bpm, 1) + " BPM"
                                : (file == juce::File() ? "--- BPM" : "analizando..."),
                      juce::dontSendNotification);

    timeLabel.setText (formatTime (deck.getPositionSeconds()) + " / " + formatTime (deck.getLengthSeconds()),
                       juce::dontSendNotification);
    tempoLabel.setText (juce::String (tempoSlider.getValue(), 2) + " %", juce::dontSendNotification);
    playButton.setButtonText (deck.isPlaying() ? "Pausa" : "Play");
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

void DeckComponent::paint (juce::Graphics& g)
{
    g.setColour (DarkLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    // Forma de onda y cabezal de reproduccion.
    g.setColour (DarkLookAndFeel::background);
    g.fillRect (waveformArea);
    auto& thumb = deck.getThumbnail();
    const double length = deck.getLengthSeconds();
    if (thumb.getTotalLength() > 0.0)
    {
        g.setColour (accent().withAlpha (0.85f));
        thumb.drawChannels (g, waveformArea.reduced (2), 0.0, thumb.getTotalLength(), 1.0f);
    }
    if (length > 0.0)
    {
        const float x = (float) waveformArea.getX() + (float) (deck.getPositionSeconds() / length) * (float) waveformArea.getWidth();
        g.setColour (DarkLookAndFeel::text);
        g.drawLine (x, (float) waveformArea.getY(), x, (float) waveformArea.getBottom(), 2.0f);
    }
}

void DeckComponent::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto header = area.removeFromTop (36);
    bpmLabel.setBounds (header.removeFromRight (180));
    titleLabel.setBounds (header);
    timeLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (8);

    auto right = area.removeFromRight (70);
    tempoLabel.setBounds (right.removeFromBottom (20));
    tempoSlider.setBounds (right);

    auto buttons = area.removeFromBottom (44);
    loadButton.setBounds (buttons.removeFromLeft (120).reduced (0, 4));
    buttons.removeFromLeft (8);
    playButton.setBounds (buttons.removeFromLeft (120).reduced (0, 4));

    area.removeFromBottom (8);
    waveformArea = area.reduced (0, 4).withTrimmedRight (8);
}

void DeckComponent::mouseDown (const juce::MouseEvent& e)
{
    if (waveformArea.contains (e.getPosition()) && deck.getLengthSeconds() > 0.0)
    {
        const double ratio = (double) (e.x - waveformArea.getX()) / (double) waveformArea.getWidth();
        deck.setPositionSeconds (ratio * deck.getLengthSeconds());
    }
}
}
