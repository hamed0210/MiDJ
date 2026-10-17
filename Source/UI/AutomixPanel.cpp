#include "UI/AutomixPanel.h"
#include "UI/DarkLookAndFeel.h"
#include "UI/Icons.h"
#include "UI/LibraryBrowser.h"
#include "Video/DeckVideo.h"

namespace midj
{
AutomixPanel::AutomixPanel (Automix& a, Library& l) : automix (a), library (l)
{
    title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (title);
    status.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
    status.setFont (juce::FontOptions (12.0f));
    status.setJustificationType (juce::Justification::topLeft);
    status.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (status);

    list.setModel (this);
    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, DarkLookAndFeel::background);
    addAndMakeVisible (list);

    onButton.setTooltip ("Encender / apagar el automix");
    onButton.onClick = [this] { automix.setEnabled (! automix.isEnabled()); updateStatus(); };
    mixNowButton.setTooltip ("Empezar ya la transicion a la siguiente");
    mixNowButton.onClick = [this] { automix.mixNow(); };
    skipButton.setTooltip ("Saltar la siguiente cancion");
    skipButton.onClick = [this] { automix.skip(); };
    removeButton.setTooltip ("Quitar de la lista la cancion elegida (Supr)");
    removeButton.onClick = [this] { if (list.getSelectedRow() >= 0) automix.remove (list.getSelectedRow()); };
    clearButton.setTooltip ("Vaciar la lista");
    clearButton.onClick = [this] { automix.clearQueue(); };
    for (int s : { 2, 4, 6, 8, 12, 16 })
        fadeBox.addItem ("Fundido " + juce::String (s) + " s", s);
    fadeBox.setTooltip ("Duracion de las transiciones con fundido (las de a tempo duran 16 pulsos)");
    fadeBox.setSelectedId ((int) automix.getFadeSeconds(), juce::dontSendNotification);
    fadeBox.onChange = [this] { automix.setFadeSeconds (fadeBox.getSelectedId()); };
    for (auto* c : std::initializer_list<juce::Component*> { &onButton, &mixNowButton, &skipButton, &removeButton, &clearButton, &fadeBox })
        addAndMakeVisible (c);

    automix.addChangeListener (this);
    updateStatus();
}

AutomixPanel::~AutomixPanel()
{
    automix.removeChangeListener (this);
    list.setModel (nullptr);
}

void AutomixPanel::addLearnTargets (std::vector<LearnTarget>& out)
{
    out.push_back ({ &onButton, "automix.toggle" });
    out.push_back ({ &mixNowButton, "automix.mixnow" });
    out.push_back ({ &skipButton, "automix.skip" });
}

void AutomixPanel::updateStatus()
{
    status.setText (automix.getStatusText(), juce::dontSendNotification);
    const bool on = automix.isEnabled();
    onButton.setButtonText (on ? "Encendido" : "Apagado");
    onButton.setColour (juce::TextButton::buttonColourId, on ? juce::Colour (0xff4caf50).withAlpha (0.8f) : DarkLookAndFeel::panel.brighter (0.15f));
    const bool canMix = on && automix.getState() == Automix::State::playing && automix.getNext() != nullptr;
    mixNowButton.setEnabled (canMix);
    skipButton.setEnabled (canMix);
    title.setText ("AUTOMIX (" + juce::String ((int) automix.getQueue().size()) + ")", juce::dontSendNotification);
}

void AutomixPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    list.updateContent();
    list.repaint();
    updateStatus();
}

int AutomixPanel::getNumRows()
{
    return (int) automix.getQueue().size();
}

void AutomixPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    const auto& queue = automix.getQueue();
    if (! juce::isPositiveAndBelow (row, (int) queue.size()))
        return;
    const auto& item = queue[(size_t) row];
    if (selected)
        g.fillAll (DarkLookAndFeel::accentA.withAlpha (0.35f));
    else if (row % 2 == 1)
        g.fillAll (DarkLookAndFeel::panel.withAlpha (0.6f));
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
    Icons::drawFileType (g, r.removeFromLeft (24.0f).reduced (4.0f), DeckVideo::isVideoFile (item.file));
    g.setFont (juce::FontOptions (13.0f));
    if (item.bpm > 0.0)
    {
        g.setColour (DarkLookAndFeel::textQuiet);
        g.drawText (juce::String (item.bpm, 0), r.removeFromRight (34.0f), juce::Justification::centredRight, false);
    }
    g.setColour (DarkLookAndFeel::text);
    const auto name = item.artist.isNotEmpty() ? item.artist + " - " + item.title
                    : item.title.isNotEmpty() ? item.title : item.file.getFileNameWithoutExtension();
    g.drawText (juce::String (row + 1) + ". " + name, r.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);
}

void AutomixPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || ! juce::isPositiveAndBelow (row, getNumRows()))
        return;
    juce::PopupMenu menu;
    menu.addItem ("Quitar de la lista", [this, row] { automix.remove (row); });
    menu.addItem ("Vaciar la lista", [this] { automix.clearQueue(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void AutomixPanel::deleteKeyPressed (int row)
{
    if (juce::isPositiveAndBelow (row, getNumRows()))
        automix.remove (row);
}

bool AutomixPanel::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.toString().startsWith (LibraryBrowser::dragPrefix);
}

void AutomixPanel::itemDropped (const SourceDetails& details)
{
    dragOver = false;
    repaint();
    const juce::File file (details.description.toString().fromFirstOccurrenceOf (LibraryBrowser::dragPrefix, false, false));
    Automix::Item item { file, file.getFileNameWithoutExtension(), {}, 0.0 };
    if (const auto t = library.findByFile (file))
        item = { file, t->title, t->artist, t->bpm };
    automix.add (item);
}

void AutomixPanel::paint (juce::Graphics& g)
{
    g.setColour (DarkLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
}

void AutomixPanel::paintOverChildren (juce::Graphics& g)
{
    if (getNumRows() == 0)
    {
        g.setColour (DarkLookAndFeel::textQuiet);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText ("Arrastra canciones aqui", list.getBounds().reduced (10), juce::Justification::centred, 3);
    }
    if (dragOver)
    {
        g.setColour (DarkLookAndFeel::accentA);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 8.0f, 3.0f);
    }
}

void AutomixPanel::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto header = r.removeFromTop (24);
    onButton.setBounds (header.removeFromRight (90).reduced (0, 1));
    title.setBounds (header);
    r.removeFromTop (4);
    status.setBounds (r.removeFromBottom (32));
    auto row2 = r.removeFromBottom (26);
    r.removeFromBottom (4);
    auto row1 = r.removeFromBottom (26);
    r.removeFromBottom (4);
    list.setBounds (r);
    const int w1 = row1.getWidth() / 2;
    mixNowButton.setBounds (row1.removeFromLeft (w1).reduced (1, 0));
    skipButton.setBounds (row1.reduced (1, 0));
    const int w2 = row2.getWidth() / 3;
    removeButton.setBounds (row2.removeFromLeft (w2).reduced (1, 0));
    clearButton.setBounds (row2.removeFromLeft (w2).reduced (1, 0));
    fadeBox.setBounds (row2.reduced (1, 0));
}
}
