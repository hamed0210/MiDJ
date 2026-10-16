#include "MainComponent.h"

namespace midj
{
MainComponent::MainComponent()
{
    setLookAndFeel (&lookAndFeel);
    formatManager.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg y MP3

    addAndMakeVisible (deckViewA);
    addAndMakeVisible (deckViewB);

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

    setSize (1200, 640);
    setAudioChannels (0, 2);   // sin entradas, salida estereo
}

MainComponent::~MainComponent()
{
    shutdownAudio();
    setLookAndFeel (nullptr);
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
    auto area = getLocalBounds().reduced (12);
    auto bottom = area.removeFromBottom (70);
    auto half = area.getWidth() / 2;
    deckViewA.setBounds (area.removeFromLeft (half).reduced (6));
    deckViewB.setBounds (area.reduced (6));

    auto fader = bottom.withSizeKeepingCentre (360, 70);
    crossfaderLabel.setBounds (fader.removeFromTop (22));
    crossfader.setBounds (fader.reduced (0, 6));
}
}
