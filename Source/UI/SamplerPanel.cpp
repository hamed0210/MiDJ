#include "UI/SamplerPanel.h"
#include "UI/DarkLookAndFeel.h"
#include "UI/LibraryBrowser.h"

namespace midj
{
namespace
{
    // Un color por pad (los mismos tonos que el resto de la interfaz).
    const std::array<juce::Colour, Sampler::numPads> padColours {
        juce::Colour (0xff3ea6ff), juce::Colour (0xffffb020), juce::Colour (0xff9b8cff), juce::Colour (0xff3ddc97),
        juce::Colour (0xffff6b8b), juce::Colour (0xff4dd0e1), juce::Colour (0xffe8c547), juce::Colour (0xffc58cf5) };

    bool isSampleFile (const juce::File& f)
    {
        return f.hasFileExtension ("vdjsample;wav;mp3;flac;ogg;aif;aiff");
    }
}

class SamplerPanel::Pad : public juce::Component,
                          public juce::DragAndDropTarget,
                          public juce::FileDragAndDropTarget,
                          public juce::SettableTooltipClient
{
public:
    Pad (SamplerPanel& o, int i) : owner (o), index (i) {}

    void repaintIfChanged()
    {
        // Lo que se ve del pad en un numero: sonido, si suena, avance (en pasos del 1 %), grabando y modos.
        auto& smp = owner.sampler;
        const auto sample = smp.getSample (index);
        const bool playing = smp.isPlaying (index);
        const juce::int64 state = (juce::int64) (juce::pointer_sized_int) sample.get() * 31
                                + (playing ? 1000 + (int) (smp.getProgress (index) * 100.0) : 0)
                                + (smp.getRecordingPad() == index ? 7 : 0) + (smp.getLoop (index) ? 11 : 0)
                                + (int) smp.getTempoMode (index) * 13 + (int) smp.getTrigger (index) * 17;
        if (state != lastState)
        {
            lastState = state;
            const juce::String name = "Pad " + juce::String (index + 1) + " (tecla " + juce::String (index + 1) + ")";
            setTooltip (sample != nullptr ? name + ": " + sample->name + ". Clic derecho: opciones"
                                          : name + " vacio: clic para cargar un sonido. Clic derecho: grabar de un deck");
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto& smp = owner.sampler;
        const auto sample = smp.getSample (index);
        const auto colour = padColours[(size_t) index];
        const bool playing = smp.isPlaying (index);
        const bool recording = smp.getRecordingPad() == index;
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const float radius = owner.compact ? 6.0f : 10.0f, inset = owner.compact ? 4.0f : 8.0f;

        // Vacio: borde de rayas; con sonido: borde de su color; sonando: lleno de su color.
        g.setColour (playing ? colour : DarkLookAndFeel::raised);
        g.fillRoundedRectangle (r, radius);
        if (dragOver)
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (r, radius, 2.0f);
        }
        else if (sample == nullptr && ! recording)
        {
            juce::Path outline, dashed;
            outline.addRoundedRectangle (r.reduced (0.5f), radius);
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (DarkLookAndFeel::line.brighter (0.25f));
            g.fillPath (dashed);
        }
        else
        {
            g.setColour (colour.withAlpha (0.9f));
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        }

        // Barra de avance abajo.
        if (playing)
        {
            g.setColour (DarkLookAndFeel::background.withAlpha (0.6f));
            g.fillRect (r.reduced (inset, 0.0f).removeFromBottom (3.0f).withWidth ((r.getWidth() - 2.0f * inset) * (float) smp.getProgress (index)));
        }

        // Compacto: solo el numero (rojo mientras graba).
        if (owner.compact)
        {
            g.setFont (DarkLookAndFeel::mono (12.0f, sample != nullptr));
            g.setColour (recording ? DarkLookAndFeel::danger : playing ? DarkLookAndFeel::background
                                   : sample != nullptr ? DarkLookAndFeel::text : DarkLookAndFeel::textFaint);
            g.drawText (juce::String (index + 1), getLocalBounds(), juce::Justification::centred);
            return;
        }

        auto text = getLocalBounds().reduced (8, 6);
        g.setFont (DarkLookAndFeel::mono (11.0f));
        g.setColour (playing ? DarkLookAndFeel::background : DarkLookAndFeel::textQuiet);
        // Pads bajos: numero y nombre en una sola linea.
        const bool oneLine = text.getHeight() < 30;
        g.drawText (juce::String (index + 1), oneLine ? text.removeFromLeft (14) : text.removeFromTop (12), oneLine ? juce::Justification::centredLeft : juce::Justification::topLeft);
        if (recording)
        {
            g.setColour (DarkLookAndFeel::danger);
            g.setFont (DarkLookAndFeel::sans (13.0f, true));
            g.drawText ("REC", text, juce::Justification::centredLeft);
            return;
        }
        if (sample == nullptr)
        {
            g.setColour (DarkLookAndFeel::textFaint);
            g.setFont (DarkLookAndFeel::sans (12.0f, true));
            g.drawText ("Vacio", text, oneLine ? juce::Justification::centredLeft : juce::Justification::bottomLeft);
            return;
        }

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
        g.setFont (DarkLookAndFeel::sans (10.0f));
        g.drawText (tags.joinIntoString (" "), getLocalBounds().reduced (8, 6).removeFromTop (12), juce::Justification::topRight);
        g.setColour (playing ? DarkLookAndFeel::background : DarkLookAndFeel::text);
        g.setFont (DarkLookAndFeel::sans (12.0f, true));
        g.drawFittedText (sample->name, text, oneLine ? juce::Justification::centredLeft : juce::Justification::bottomLeft, oneLine ? 1 : 2);
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
    juce::int64 lastState = -1;
};

SamplerPanel::SamplerPanel (Sampler& s, juce::AudioFormatManager& f) : sampler (s), formats (f)
{

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
    // Solo los pads que cambian (los que suenan, por su barra de avance).
    for (auto& p : pads)
        p->repaintIfChanged();
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
    if (compact)
    {
        g.setColour (DarkLookAndFeel::textQuiet);
        g.setFont (DarkLookAndFeel::sans (11.0f, true).withKerningFactor (0.08f));
        g.drawText ("SAMPLER", titleArea, juce::Justification::centredLeft, false);
        return;
    }
    g.setColour (DarkLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 14.0f);
    g.setColour (DarkLookAndFeel::text);
    g.setFont (DarkLookAndFeel::sans (14.0f, true));
    g.drawText ("Sampler", titleArea, juce::Justification::centredLeft, false);
    g.setColour (DarkLookAndFeel::textQuiet);
    g.setFont (DarkLookAndFeel::sans (12.0f));
    g.drawText ("Teclas 1-8", titleArea.withTrimmedLeft (70), juce::Justification::centredLeft, false);
    g.drawText ("Volumen", volumeLabelArea, juce::Justification::centredRight, false);
}

void SamplerPanel::resized()
{
    if (compact)
    {
        // Una fila: SAMPLER, los 8 pads, el volumen y "Parar".
        auto row = getLocalBounds();
        titleArea = row.removeFromLeft (62);
        for (auto& p : pads)
        {
            p->setBounds (row.removeFromLeft (34));
            row.removeFromLeft (4);
        }
        row.removeFromLeft (6);
        volume.setBounds (row.removeFromLeft (72));
        row.removeFromLeft (8);
        stopButton.setBounds (row.removeFromLeft (56));
        stopButton.setButtonText ("Parar");
        volumeLabelArea = {};
        return;
    }
    stopButton.setButtonText ("Parar todo");
    // Arriba el nombre, el volumen y "Parar todo"; debajo los 8 pads en fila (o en 2 filas si es estrecho).
    auto area = getLocalBounds().reduced (14, 10);
    auto header = area.removeFromTop (28);
    stopButton.setBounds (header.removeFromRight (92));
    header.removeFromRight (10);
    volume.setBounds (header.removeFromRight (juce::jmin (130, header.getWidth() / 3)));
    volumeLabelArea = header.removeFromRight (60);
    titleArea = header;
    area.removeFromTop (8);

    const int cols = area.getWidth() >= 8 * 56 ? 8 : 4, rows = Sampler::numPads / cols, gap = 8;
    const int w = (area.getWidth() - gap * (cols - 1)) / cols, h = (area.getHeight() - gap * (rows - 1)) / rows;
    for (int i = 0; i < Sampler::numPads; ++i)
        pads[(size_t) i]->setBounds (area.getX() + (i % cols) * (w + gap), area.getY() + (i / cols) * (h + gap), w, h);
}
}
