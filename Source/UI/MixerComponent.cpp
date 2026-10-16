#include "UI/MixerComponent.h"
#include "UI/DarkLookAndFeel.h"

namespace midj
{
MixerComponent::MixerComponent (Mixer& mixer)
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
}

void MixerComponent::resized()
{
    // Tres columnas: mandos del deck A | nombres | mandos del deck B. Debajo, los dos faders.
    auto area = getLocalBounds().reduced (8);
    const int colWidth = area.getWidth() / 3;
    auto faders = area.removeFromBottom (juce::jmax (90, area.getHeight() * 30 / 100));
    const int rowHeight = area.getHeight() / (int) knobNames.size();

    for (size_t i = 0; i < knobNames.size(); ++i)
    {
        auto row = area.removeFromTop (rowHeight);
        channels[0].knobs[i].setBounds (row.removeFromLeft (colWidth).reduced (4));
        channels[1].knobs[i].setBounds (row.removeFromRight (colWidth).reduced (4));
        rowLabels[i].setBounds (row);
    }

    faders.removeFromTop (6);
    channels[0].volume.setBounds (faders.removeFromLeft (colWidth));
    channels[1].volume.setBounds (faders.removeFromRight (colWidth));
    volumeLabel.setBounds (faders.withSizeKeepingCentre (faders.getWidth(), 20));
}
}
