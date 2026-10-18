#include "UI/AutomixPanel.h"
#include "UI/DarkLookAndFeel.h"
#include "UI/Icons.h"
#include "UI/LibraryBrowser.h"
#include "Video/DeckVideo.h"

namespace midj
{
AutomixPanel::AutomixPanel (Automix& a, Library& l) : automix (a), library (l)
{
    title.setFont (DarkLookAndFeel::sans (14.0f, true));
    addAndMakeVisible (title);
    status.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
    status.setFont (DarkLookAndFeel::sans (12.0f));
    status.setJustificationType (juce::Justification::topLeft);
    status.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (status);

    list.setModel (this);
    list.setRowHeight (40);
    list.setColour (juce::ListBox::backgroundColourId, DarkLookAndFeel::panel);
    addAndMakeVisible (list);

    onButton.setTooltip ("Encender / apagar el automix");
    onButton.onClick = [this] { automix.setEnabled (! automix.isEnabled()); updateStatus(); };
    mixNowButton.setBordered (true, DarkLookAndFeel::text);
    mixNowButton.onClick = [this] { automix.mixNow(); };
    skipButton.setBordered (true);
    skipButton.onClick = [this] { automix.skip(); };
    clearButton.setBordered (true);
    clearButton.onClick = [this] { automix.clearQueue(); };
    for (int s : { 2, 4, 6, 8, 12, 16 })
        fadeBox.addItem ("Fundido " + juce::String (s) + " s", s);
    fadeBox.setTooltip ("Duracion de las transiciones con fundido (las de a tempo duran 16 pulsos)");
    fadeBox.setSelectedId ((int) automix.getFadeSeconds(), juce::dontSendNotification);
    fadeBox.onChange = [this] { automix.setFadeSeconds (fadeBox.getSelectedId()); };
    listsButton.setTooltip ("Reproducir o anadir los Favoritos o los Mas reproducidos");
    listsButton.onClick = [this] { showListsMenu(); };
    for (auto* c : std::initializer_list<juce::Component*> { &onButton, &listsButton, &mixNowButton, &skipButton, &clearButton, &fadeBox })
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

void AutomixPanel::showListsMenu()
{
    // Reproducir = la lista pasa a ser esa y el automix se enciende; Anadir = al final.
    enum { playFavorites = 1, addFavorites, playMostPlayed, addMostPlayed };
    const int favorites = library.countFavorites(), played = library.countPlayed();
    juce::PopupMenu menu;
    menu.addSectionHeader ("Favoritos (" + juce::String (favorites) + ")");
    menu.addItem (playFavorites, "Reproducir Favoritos", favorites > 0);
    menu.addItem (addFavorites, "Anadir Favoritos al final", favorites > 0);
    menu.addSectionHeader ("Mas reproducidos (" + juce::String (played) + ")");
    menu.addItem (playMostPlayed, "Reproducir Mas reproducidos", played > 0);
    menu.addItem (addMostPlayed, "Anadir Mas reproducidos al final", played > 0);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&listsButton),
                        [safe = juce::Component::SafePointer<AutomixPanel> (this)] (int r)
                        {
                            if (safe == nullptr || r <= 0)
                                return;
                            auto& panel = *safe;
                            const bool favs = r == playFavorites || r == addFavorites;
                            const bool replace = r == playFavorites || r == playMostPlayed;
                            const auto tracks = favs ? panel.library.getFavorites() : panel.library.getMostPlayed();
                            if (replace)
                                panel.automix.clearQueue();
                            for (const auto& t : tracks)
                                panel.automix.add ({ t.getFile(), t.title, t.artist, t.bpm });
                            if (replace && ! panel.automix.isEnabled())
                                panel.automix.setEnabled (true);
                            panel.updateStatus();
                        });
}

void AutomixPanel::updateStatus()
{
    status.setText (automix.getStatusText(), juce::dontSendNotification);
    const bool on = automix.isEnabled();
    onButton.setButtonText (on ? "Encendido" : "Apagado");
    onButton.getProperties().set (DarkLookAndFeel::filledProperty, on);
    onButton.setColour (juce::TextButton::buttonColourId, on ? DarkLookAndFeel::good : juce::Colours::transparentBlack);
    onButton.setColour (juce::TextButton::textColourOffId, on ? DarkLookAndFeel::background : DarkLookAndFeel::text);
    const bool canMix = on && automix.getState() == Automix::State::playing && automix.getNext() != nullptr;
    mixNowButton.setEnabled (canMix);
    skipButton.setEnabled (canMix);
    clearButton.setEnabled (! automix.getQueue().empty());
    title.setText ("Automix  " + juce::String ((int) automix.getQueue().size()), juce::dontSendNotification);
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
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.0f, 2.0f);
    if (selected || row == 0)
    {
        g.setColour (selected ? DarkLookAndFeel::selection : DarkLookAndFeel::raised);
        g.fillRoundedRectangle (r, 8.0f);
    }
    r.reduce (8.0f, 0.0f);
    g.setColour (DarkLookAndFeel::textQuiet);
    g.setFont (DarkLookAndFeel::mono (12.0f));
    g.drawText (juce::String (row + 1), r.removeFromLeft (18.0f), juce::Justification::centredLeft, false);
    Icons::drawFileType (g, r.removeFromLeft (18.0f).reduced (1.0f, 10.0f), DeckVideo::isVideoFile (item.file));
    r.removeFromLeft (6.0f);
    Icons::draw (g, r.removeFromRight (16.0f).withSizeKeepingCentre (12.0f, 12.0f), Icons::Glyph::close, DarkLookAndFeel::textQuiet);   // quitar
    r.removeFromRight (6.0f);

    const auto name = item.title.isNotEmpty() ? item.title : item.file.getFileNameWithoutExtension();
    g.setColour (DarkLookAndFeel::text);
    g.setFont (DarkLookAndFeel::sans (13.0f, true));
    g.drawText (name, r.removeFromTop (r.getHeight() / 2.0f + 1.0f), juce::Justification::bottomLeft, true);
    juce::String sub = item.artist;
    if (item.bpm > 0.0)
        sub << (sub.isNotEmpty() ? "  " : "") << juce::String (item.bpm, 0) << " BPM";
    g.setColour (DarkLookAndFeel::textQuiet);
    g.setFont (DarkLookAndFeel::sans (12.0f));
    g.drawText (sub, r, juce::Justification::topLeft, true);
}

void AutomixPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, getNumRows()))
        return;
    // La x de la derecha de cada fila la quita de la lista.
    if (! e.mods.isPopupMenu() && e.x >= list.getVisibleRowWidth() - 32)
    {
        automix.remove (row);
        return;
    }
    if (! e.mods.isPopupMenu())
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
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 14.0f);
}

void AutomixPanel::paintOverChildren (juce::Graphics& g)
{
    if (getNumRows() == 0)
    {
        const auto r = list.getBounds().toFloat().reduced (2.0f);
        juce::Path outline, dashed;
        outline.addRoundedRectangle (r, 10.0f);
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (DarkLookAndFeel::line.brighter (0.25f));
        g.fillPath (dashed);
        g.setColour (DarkLookAndFeel::textQuiet);
        g.setFont (DarkLookAndFeel::sans (13.0f));
        g.drawFittedText ("Arrastra canciones aqui\no pulsa A sobre una cancion", list.getBounds().reduced (12), juce::Justification::centred, 3);
    }
    if (dragOver)
    {
        g.setColour (DarkLookAndFeel::accentA);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 14.0f, 3.0f);
    }
}

void AutomixPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto header = r.removeFromTop (30);
    onButton.setBounds (header.removeFromRight (96).reduced (0, 1));
    header.removeFromRight (6);
    listsButton.setBounds (header.removeFromRight (64).reduced (0, 1));
    title.setBounds (header);
    r.removeFromTop (8);
    status.setBounds (r.removeFromBottom (30));
    r.removeFromBottom (4);
    auto buttons = r.removeFromBottom (40);
    r.removeFromBottom (8);
    list.setBounds (r);
    // Mezclar ya | Saltar | Vaciar como iconos y, a la derecha, la duracion del fundido.
    auto fade = buttons.removeFromRight (juce::jmin (120, buttons.getWidth() / 3));
    fadeBox.setBounds (fade.withSizeKeepingCentre (fade.getWidth(), 30));
    buttons.removeFromRight (8);
    const int w = (buttons.getWidth() - 12) / 3;
    mixNowButton.setBounds (buttons.removeFromLeft (w));
    buttons.removeFromLeft (6);
    skipButton.setBounds (buttons.removeFromLeft (w));
    buttons.removeFromLeft (6);
    clearButton.setBounds (buttons);
}
}
