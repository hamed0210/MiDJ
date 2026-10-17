#include "UI/SamplerPanel.h"
#include "UI/DarkLookAndFeel.h"
#include "UI/LibraryBrowser.h"

namespace midj
{
namespace
{
    // Un color por pad, como los pads de una controladora.
    const std::array<juce::Colour, Sampler::numPads> padColours {
        juce::Colour (0xff2f9be0), juce::Colour (0xff26a69a), juce::Colour (0xff9ccc65), juce::Colour (0xffffca28),
        juce::Colour (0xffffa726), juce::Colour (0xffef5350), juce::Colour (0xffab47bc), juce::Colour (0xff5c6bc0) };

    bool isSampleFile (const juce::File& f)
    {
        return f.hasFileExtension ("vdjsample;wav;mp3;flac;ogg;aif;aiff");
    }
}

class SamplerPanel::Pad : public juce::Component,
                          public juce::DragAndDropTarget,
                          public juce::FileDragAndDropTarget
{
public:
    Pad (SamplerPanel& o, int i) : owner (o), index (i) {}

    void paint (juce::Graphics& g) override
    {
        auto& smp = owner.sampler;
        const auto sample = smp.getSample (index);
        const auto colour = padColours[(size_t) index];
        const bool playing = smp.isPlaying (index);
        const bool recording = smp.getRecordingPad() == index;
        auto r = getLocalBounds().toFloat().reduced (2.0f);

        g.setColour (sample == nullptr ? DarkLookAndFeel::background
                                       : colour.withAlpha (playing ? 0.85f : 0.28f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (dragOver ? juce::Colours::white : colour.withAlpha (sample == nullptr ? 0.35f : 0.9f));
        g.drawRoundedRectangle (r, 5.0f, dragOver ? 2.5f : 1.2f);

        // Barra de avance abajo.
        if (playing)
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillRect (r.removeFromBottom (3.0f).withWidth (r.getWidth() * (float) smp.getProgress (index)));
        }

        auto text = getLocalBounds().reduced (6, 4);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (DarkLookAndFeel::textQuiet);
        g.drawText (juce::String (index + 1), text.removeFromTop (12), juce::Justification::topLeft);
        if (recording)
        {
            g.setColour (juce::Colour (0xffef5350));
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText ("REC", text, juce::Justification::centred);
            return;
        }
        if (sample == nullptr)
            return;

        // Indicadores de modo en una linea pequena.
        juce::StringArray tags;
        if (smp.getLoop (index)) tags.add ("loop");
        switch (smp.getTempoMode (index))
        {
            case Sampler::TempoMode::sync:   tags.add ("sync"); break;
            case Sampler::TempoMode::locked: tags.add ("lock"); break;
            default: break;
        }
        switch (smp.getTrigger (index))
        {
            case Sampler::Trigger::hold:    tags.add ("hold"); break;
            case Sampler::Trigger::stutter: tags.add ("stut"); break;
            default: break;
        }
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (tags.joinIntoString (" "), text.removeFromBottom (12), juce::Justification::bottomRight);
        g.setColour (playing ? juce::Colours::black : DarkLookAndFeel::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawFittedText (sample->name, text, juce::Justification::centred, 2);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            owner.showMenu (index);
        else if (owner.sampler.getSample (index) == nullptr)
            owner.choosePadFile (index);
        else
            owner.sampler.press (index);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            owner.sampler.release (index);
    }

    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return d.description.toString().startsWith (LibraryBrowser::dragPrefix);
    }
    void itemDragEnter (const SourceDetails&) override { dragOver = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dragOver = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dragOver = false;
        owner.loadIntoPad (index, juce::File (d.description.toString().fromFirstOccurrenceOf (LibraryBrowser::dragPrefix, false, false)));
    }
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() == 1 && isSampleFile (juce::File (files[0]));
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override           { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragOver = false;
        owner.loadIntoPad (index, juce::File (files[0]));
    }

private:
    SamplerPanel& owner;
    const int index;
    bool dragOver = false;
};

SamplerPanel::SamplerPanel (Sampler& s, juce::AudioFormatManager& f) : sampler (s), formats (f)
{
    title.setText ("SAMPLER", juce::dontSendNotification);
    title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
    addAndMakeVisible (title);

    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volume.setRange (0.0, 1.0);
    volume.setValue (sampler.getVolume(), juce::dontSendNotification);
    volume.setDoubleClickReturnValue (true, 0.8);
    volume.setTooltip ("Volumen del sampler");
    volume.onValueChange = [this] { sampler.setVolume ((float) volume.getValue()); };
    addAndMakeVisible (volume);

    stopButton.setTooltip ("Parar todos los pads");
    stopButton.onClick = [this] { sampler.stopAll(); };
    addAndMakeVisible (stopButton);

    for (int i = 0; i < Sampler::numPads; ++i)
    {
        pads[(size_t) i] = std::make_unique<Pad> (*this, i);
        addAndMakeVisible (*pads[(size_t) i]);
    }

    sampler.onRecordingFinished = [this] (int)
    {
        if (onBankChanged)
            onBankChanged();
    };
    startTimerHz (20);
}

SamplerPanel::~SamplerPanel()
{
    sampler.onRecordingFinished = nullptr;
}

void SamplerPanel::timerCallback()
{
    for (auto& p : pads)
        p->repaint();
    if (! volume.isMouseButtonDown())
        volume.setValue (sampler.getVolume(), juce::dontSendNotification);   // la controladora tambien lo mueve
}

void SamplerPanel::loadIntoPad (int pad, const juce::File& file)
{
    auto s = SampleData::loadFile (file, formats);
    if (s == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MiDJ",
                                                "No se pudo cargar en el pad:\n" + file.getFullPathName());
        return;
    }
    sampler.setSample (pad, s);
    if (onBankChanged)
        onBankChanged();
}

void SamplerPanel::choosePadFile (int pad)
{
    chooser = std::make_unique<juce::FileChooser> ("Sonido para el pad " + juce::String (pad + 1),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.vdjsample;*.wav;*.mp3;*.flac;*.ogg;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, pad] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  loadIntoPad (pad, fc.getResult());
                          });
}

void SamplerPanel::showMenu (int pad)
{
    // Numeros de las opciones: grabar = 1000 + 100 x deck + pulsos; disparo 200-202; tempo 300-302.
    enum { load = 1, clear, loopItem, triggerBase = 200, tempoBase = 300, recordBase = 1000 };
    const bool has = sampler.getSample (pad) != nullptr;

    juce::PopupMenu record;
    for (int deck = 0; deck < 2; ++deck)
        for (int beats : { 4, 8, 16 })
            record.addItem (recordBase + deck * 100 + beats, "Deck " + juce::String (deck == 0 ? "A" : "B") + ": "
                                                                 + juce::String (beats) + " pulsos");
    juce::PopupMenu trigger, tempo;
    const auto t = sampler.getTrigger (pad);
    trigger.addItem (triggerBase, "On / off", true, t == Sampler::Trigger::onOff);
    trigger.addItem (triggerBase + 1, "Mantener (hold)", true, t == Sampler::Trigger::hold);
    trigger.addItem (triggerBase + 2, "Repetir (stutter)", true, t == Sampler::Trigger::stutter);
    const auto m = sampler.getTempoMode (pad);
    tempo.addItem (tempoBase, "Libre", true, m == Sampler::TempoMode::free);
    tempo.addItem (tempoBase + 1, "Sincronizado al deck", has && sampler.getSample (pad)->bpm > 0.0, m == Sampler::TempoMode::sync);
    tempo.addItem (tempoBase + 2, "Bloqueado al pulso", has && sampler.getSample (pad)->bpm > 0.0, m == Sampler::TempoMode::locked);

    juce::PopupMenu menu;
    menu.addSectionHeader ("Pad " + juce::String (pad + 1));
    menu.addItem (load, "Cargar archivo...");
    menu.addSubMenu ("Grabar del deck", record);
    menu.addSeparator();
    menu.addSubMenu ("Disparo", trigger, has);
    menu.addItem (loopItem, "Bucle", has, sampler.getLoop (pad));
    menu.addSubMenu ("Tempo", tempo, has);
    menu.addSeparator();
    menu.addItem (clear, "Quitar", has);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (pads[(size_t) pad].get()),
                        [this, pad] (int r)
                        {
                            if (r == load)
                                choosePadFile (pad);
                            else if (r == clear)
                                sampler.clearPad (pad);
                            else if (r == loopItem)
                                sampler.setLoop (pad, ! sampler.getLoop (pad));
                            else if (r >= triggerBase && r <= triggerBase + 2)
                                sampler.setTrigger (pad, (Sampler::Trigger) (r - triggerBase));
                            else if (r >= tempoBase && r <= tempoBase + 2)
                                sampler.setTempoMode (pad, (Sampler::TempoMode) (r - tempoBase));
                            else if (r > recordBase && r < recordBase + 200)
                                sampler.startRecording (pad, (r - recordBase) / 100, (r - recordBase) % 100);
                            else
                                return;
                            if (onBankChanged && r != load)
                                onBankChanged();
                        });
}

void SamplerPanel::addLearnTargets (std::vector<LearnTarget>& out)
{
    for (int i = 0; i < Sampler::numPads; ++i)
        out.push_back ({ pads[(size_t) i].get(), "sampler.pad" + juce::String (i + 1) });
    out.push_back ({ &stopButton, "sampler.stop" });
    out.push_back ({ &volume, "sampler.volume" });
}

void SamplerPanel::paint (juce::Graphics& g)
{
    g.setColour (DarkLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
}

void SamplerPanel::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto header = area.removeFromTop (28);
    title.setBounds (header.removeFromLeft (76));
    stopButton.setBounds (header.removeFromRight (56).reduced (0, 2));
    header.removeFromRight (6);
    volume.setBounds (header);
    area.removeFromTop (6);

    const int cols = 4, rows = Sampler::numPads / cols;
    const int w = area.getWidth() / cols, h = area.getHeight() / rows;
    for (int i = 0; i < Sampler::numPads; ++i)
        pads[(size_t) i]->setBounds (area.getX() + (i % cols) * w, area.getY() + (i / cols) * h, w, h);
}
}
