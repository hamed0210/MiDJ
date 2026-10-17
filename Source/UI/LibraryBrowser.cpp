#include "UI/LibraryBrowser.h"
#include "UI/DarkLookAndFeel.h"

namespace midj
{
namespace
{
    juce::String formatDuration (double seconds)
    {
        const int total = (int) std::round (seconds);
        return juce::String::formatted ("%d:%02d", total / 60, total % 60);
    }
}

LibraryBrowser::LibraryBrowser (Library& l) : library (l)
{
    library.addChangeListener (this);

    sourceList.setRowHeight (24);
    sourceList.setColour (juce::ListBox::backgroundColourId, DarkLookAndFeel::panel);
    addAndMakeVisible (sourceList);

    addFolderButton.onClick = [this] { chooseFolder(); };
    addFolderButton.setTooltip ("Agregar una carpeta de musica a la biblioteca");
    removeFolderButton.onClick = [this] { removeSelectedFolder(); };
    removeFolderButton.setTooltip ("Quitar la carpeta elegida de la biblioteca (no borra archivos)");
    addAndMakeVisible (addFolderButton);
    addAndMakeVisible (removeFolderButton);

    statusLabel.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
    statusLabel.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (statusLabel);

    searchBox.setTextToShowWhenEmpty ("Buscar (titulo, artista, album, genero)...", DarkLookAndFeel::textQuiet);
    searchBox.setColour (juce::TextEditor::backgroundColourId, DarkLookAndFeel::background);
    searchBox.setColour (juce::TextEditor::outlineColourId, DarkLookAndFeel::panel.brighter (0.3f));
    searchBox.setFont (juce::FontOptions (15.0f));
    searchBox.onTextChange = [this] { startTimer (200); };   // espera a que se deje de teclear
    searchBox.onEscapeKey = [this] { searchBox.clear(); refresh(); };
    addAndMakeVisible (searchBox);

    auto& header = table.getHeader();
    const int columnFlags = juce::TableHeaderComponent::defaultFlags;
    header.addColumn ("Titulo", titleCol, 260, 80, -1, columnFlags);
    header.addColumn ("Artista", artistCol, 190, 60, -1, columnFlags);
    header.addColumn ("BPM", bpmCol, 70, 50, 90, columnFlags);
    header.addColumn ("Duracion", durationCol, 75, 50, 90, columnFlags);
    header.addColumn ("Album", albumCol, 160, 60, -1, columnFlags);
    header.addColumn ("Genero", genreCol, 110, 60, -1, columnFlags);
    header.setSortColumnId (artistCol, true);
    header.setStretchToFitActive (true);
    header.setColour (juce::TableHeaderComponent::backgroundColourId, DarkLookAndFeel::panel.brighter (0.12f));
    header.setColour (juce::TableHeaderComponent::textColourId, DarkLookAndFeel::textQuiet);
    header.setColour (juce::TableHeaderComponent::outlineColourId, DarkLookAndFeel::background);
    header.setColour (juce::TableHeaderComponent::highlightColourId, DarkLookAndFeel::accentA.withAlpha (0.3f));
    table.setRowHeight (24);
    table.setMultipleSelectionEnabled (false);
    table.setColour (juce::ListBox::backgroundColourId, DarkLookAndFeel::background);
    addAndMakeVisible (table);

    // Barra del automix (solo se ve con la lista de automix elegida).
    automixButton.setClickingTogglesState (false);
    automixButton.setTooltip ("Encender / apagar el automix");
    automixButton.onClick = [this] { if (automix != nullptr) automix->setEnabled (! automix->isEnabled()); };
    mixNowButton.setTooltip ("Empezar ya la transicion a la siguiente");
    mixNowButton.onClick = [this] { if (automix != nullptr) automix->mixNow(); };
    skipButton.setTooltip ("Saltar la siguiente cancion");
    skipButton.onClick = [this] { if (automix != nullptr) automix->skip(); };
    removeButton.setTooltip ("Quitar de la lista la cancion elegida");
    removeButton.onClick = [this] { if (automix != nullptr && table.getSelectedRow() >= 0) automix->remove (table.getSelectedRow()); };
    clearButton.setTooltip ("Vaciar la lista");
    clearButton.onClick = [this] { if (automix != nullptr) automix->clearQueue(); };
    for (int s : { 2, 4, 6, 8, 12, 16 })
        fadeBox.addItem ("Fundido " + juce::String (s) + " s", s);
    fadeBox.setTooltip ("Duracion de las transiciones con fundido (las de a tempo duran 16 pulsos)");
    fadeBox.onChange = [this] { if (automix != nullptr) automix->setFadeSeconds (fadeBox.getSelectedId()); };
    automixStatus.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
    automixStatus.setFont (juce::FontOptions (13.0f));
    for (auto* c : std::initializer_list<juce::Component*> { &automixButton, &mixNowButton, &skipButton, &removeButton,
                                                               &clearButton, &fadeBox, &automixStatus })
        addChildComponent (c);

    refreshFolders();
    sourceList.selectRow (0);
    refresh();
}

void LibraryBrowser::setAutomix (Automix* a)
{
    if (automix != nullptr)
        automix->removeChangeListener (this);
    automix = a;
    if (automix != nullptr)
    {
        automix->addChangeListener (this);
        fadeBox.setSelectedId ((int) automix->getFadeSeconds(), juce::dontSendNotification);
    }
    sourceList.updateContent();
    refresh();
}

void LibraryBrowser::addLearnTargets (std::vector<LearnTarget>& out)
{
    out.push_back ({ &table, "browser.scroll" });
    out.push_back ({ &automixButton, "automix.toggle" });
    out.push_back ({ &mixNowButton, "automix.mixnow" });
    out.push_back ({ &skipButton, "automix.skip" });
}

void LibraryBrowser::updateAutomixStatus()
{
    if (automix == nullptr)
        return;
    automixStatus.setText (automix->getStatusText(), juce::dontSendNotification);
    const bool on = automix->isEnabled();
    automixButton.setColour (juce::TextButton::buttonColourId, on ? juce::Colour (0xff4caf50).withAlpha (0.8f) : DarkLookAndFeel::panel.brighter (0.15f));
    mixNowButton.setEnabled (on && automix->getState() == Automix::State::playing && automix->getNext() != nullptr);
    skipButton.setEnabled (on && automix->getState() == Automix::State::playing && automix->getNext() != nullptr);
}

LibraryBrowser::~LibraryBrowser()
{
    if (automix != nullptr)
        automix->removeChangeListener (this);
    library.removeChangeListener (this);
}

void LibraryBrowser::SourceList::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (selected)
        g.fillAll (DarkLookAndFeel::accentA.withAlpha (0.35f));
    g.setColour (DarkLookAndFeel::text);
    const bool automixRow = owner.automix != nullptr && row == owner.folders.size() + 1;
    g.setFont (juce::FontOptions (14.0f, row == 0 || automixRow ? juce::Font::bold : juce::Font::plain));
    if (automixRow)
        g.setColour (owner.automix->isEnabled() ? juce::Colour (0xff4caf50) : DarkLookAndFeel::text);
    const auto name = row == 0 ? juce::String ("Toda la musica")
                    : automixRow ? "Automix (" + juce::String ((int) owner.automix->getQueue().size()) + ")"
                                 : owner.folders[row - 1].getFileName();
    g.drawText (name, 8, 0, width - 12, height, juce::Justification::centredLeft, true);
}

juce::String LibraryBrowser::SourceList::getTooltipForRow (int row)
{
    if (row == 0)
        return "Todas las canciones de todas las carpetas";
    if (row > owner.folders.size())
        return "Lista de automix: clic derecho en una cancion > Agregar a automix";
    return owner.folders[row - 1].getFullPathName();
}

juce::File LibraryBrowser::selectedFolder() const
{
    const int row = sourceList.getSelectedRow();
    return row >= 1 && row <= folders.size() ? folders[row - 1] : juce::File();
}

void LibraryBrowser::refreshFolders()
{
    const auto selected = selectedFolder();
    folders = library.getFolders();
    sourceList.updateContent();
    const int index = folders.indexOf (selected);
    sourceList.selectRow (index >= 0 ? index + 1 : 0, false, true);
    removeFolderButton.setEnabled (sourceList.getSelectedRow() >= 1);
}

void LibraryBrowser::refresh()
{
    stopTimer();
    // Se conserva la cancion elegida (por id) aunque la lista cambie.
    const int row = table.getSelectedRow();
    const auto selectedId = row >= 0 && row < (int) tracks.size() ? tracks[(size_t) row].id : 0;

    const bool automixView = isAutomixView();
    if (automixView)
    {
        // La lista de automix en su orden, con los datos de la biblioteca si los tiene.
        tracks.clear();
        juce::int64 fakeId = -1;
        for (const auto& item : automix->getQueue())
        {
            auto info = library.findByFile (item.file);
            TrackInfo t = info ? *info : TrackInfo {};
            if (! info)
            {
                t.path = item.file.getFullPathName();
                t.title = item.title;
                t.artist = item.artist;
                t.bpm = item.bpm;
                t.analysed = item.bpm > 0.0;
            }
            t.id = fakeId--;   // ids unicos aunque la misma cancion este dos veces
            tracks.push_back (t);
        }
    }
    else
        tracks = library.search (searchBox.getText(), selectedFolder(), sort, ascending);
    searchBox.setVisible (! automixView);
    for (auto* c : std::initializer_list<juce::Component*> { &automixButton, &mixNowButton, &skipButton, &removeButton,
                                                               &clearButton, &fadeBox, &automixStatus })
        c->setVisible (automixView);
    updateAutomixStatus();
    sourceList.repaint();
    table.updateContent();
    table.repaint();
    removeFolderButton.setEnabled (selectedFolder() != juce::File());

    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == selectedId)
            table.selectRow ((int) i, true, true);
    statusLabel.setText (library.getStatusText(), juce::dontSendNotification);
}

void LibraryBrowser::moveSelection (int steps)
{
    if (tracks.empty())
        return;
    const int current = table.getSelectedRow();
    const int row = juce::jlimit (0, (int) tracks.size() - 1, current < 0 ? 0 : current + steps);
    table.selectRow (row);   // tambien desplaza la lista para que se vea
}

std::optional<TrackInfo> LibraryBrowser::getSelectedTrack() const
{
    const int row = table.getSelectedRow();
    if (row >= 0 && row < (int) tracks.size())
        return tracks[(size_t) row];
    return std::nullopt;
}

void LibraryBrowser::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Elige una carpeta de musica",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();
                              if (! folder.isDirectory())
                                  return;
                              library.addFolder (folder);
                              library.startBackgroundWork();
                              refreshFolders();
                              sourceList.selectRow (folders.indexOf (folder) + 1);
                          });
}

void LibraryBrowser::removeSelectedFolder()
{
    const auto folder = selectedFolder();
    if (folder == juce::File())
        return;
    library.removeFolder (folder);
    refreshFolders();
    refresh();
}

void LibraryBrowser::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (automix != nullptr && source == automix)
    {
        sourceList.repaint();   // el numero de canciones de la lista
        updateAutomixStatus();
        if (isAutomixView())
            refresh();
        return;
    }
    // La biblioteca avisa a menudo mientras escanea: se agrupan los avisos.
    statusLabel.setText (library.getStatusText(), juce::dontSendNotification);
    if (! isTimerRunning())
        startTimer (400);
}

void LibraryBrowser::timerCallback()
{
    if (library.getFolders().size() != folders.size())
        refreshFolders();
    refresh();
}

void LibraryBrowser::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    if (selected)
        g.fillAll (DarkLookAndFeel::accentA.withAlpha (0.35f));
    else if (row % 2 == 1)
        g.fillAll (DarkLookAndFeel::panel.withAlpha (0.6f));
}

void LibraryBrowser::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= (int) tracks.size())
        return;
    const auto& t = tracks[(size_t) row];
    juce::String text;
    auto justification = juce::Justification::centredLeft;
    g.setColour (DarkLookAndFeel::text);
    switch (column)
    {
        case titleCol:    text = t.title; break;
        case artistCol:   text = t.artist; break;
        case albumCol:    text = t.album; break;
        case genreCol:    text = t.genre; break;
        case durationCol: text = formatDuration (t.duration); justification = juce::Justification::centredRight; break;
        case bpmCol:
            justification = juce::Justification::centredRight;
            if (t.analysed && t.bpm > 0.0)
                text = juce::String (t.bpm, 1);
            else
            {
                text = "...";   // aun sin analizar
                g.setColour (DarkLookAndFeel::textQuiet);
            }
            break;
        default: break;
    }
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (text, 6, 0, width - 12, height, justification, true);
}

void LibraryBrowser::sortOrderChanged (int columnId, bool forwards)
{
    if (isAutomixView())
        return;   // la lista de automix va en su orden
    switch (columnId)
    {
        case titleCol:    sort = Library::SortColumn::title; break;
        case artistCol:   sort = Library::SortColumn::artist; break;
        case bpmCol:      sort = Library::SortColumn::bpm; break;
        case durationCol: sort = Library::SortColumn::duration; break;
        case albumCol:    sort = Library::SortColumn::album; break;
        case genreCol:    sort = Library::SortColumn::genre; break;
        default: break;
    }
    ascending = forwards;
    refresh();
}

void LibraryBrowser::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) tracks.size() && onLoadRequest)
        onLoadRequest (tracks[(size_t) row]);
}

void LibraryBrowser::cellClicked (int row, int, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && row >= 0 && row < (int) tracks.size())
    {
        table.selectRow (row);
        showTrackMenu (row);
    }
}

void LibraryBrowser::showTrackMenu (int row)
{
    enum { addToAutomix = 1, playNext, loadA, loadB, removeFromAutomix, prepareStems };
    juce::PopupMenu menu;
    if (isAutomixView())
        menu.addItem (removeFromAutomix, "Quitar de la lista");
    else if (automix != nullptr)
    {
        menu.addItem (addToAutomix, "Agregar a automix");
        menu.addItem (playNext, "Agregar a automix como la siguiente");
    }
    menu.addSeparator();
    menu.addItem (loadA, "Cargar en el deck A");
    menu.addItem (loadB, "Cargar en el deck B");
    if (onPrepareStems)
    {
        menu.addSeparator();
        menu.addItem (prepareStems, "Preparar stems (voz / instrumental) en segundo plano");
    }
    const auto track = tracks[(size_t) row];
    menu.showMenuAsync (juce::PopupMenu::Options(), [this, track, row] (int r)
    {
        if (r == addToAutomix && automix != nullptr)          automix->add (itemFor (track));
        else if (r == playNext && automix != nullptr)         automix->insertNext (itemFor (track));
        else if (r == removeFromAutomix && automix != nullptr) automix->remove (row);
        else if ((r == loadA || r == loadB) && onLoadToDeck)  onLoadToDeck (track, r == loadA ? 0 : 1);
        else if (r == prepareStems && onPrepareStems)          onPrepareStems (track);
    });
}

juce::var LibraryBrowser::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.isEmpty() || rows[0] >= (int) tracks.size())
        return {};
    return juce::String (dragPrefix) + tracks[(size_t) rows[0]].path;
}

void LibraryBrowser::paint (juce::Graphics& g)
{
    g.setColour (DarkLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
}

void LibraryBrowser::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto left = area.removeFromLeft (200);
    area.removeFromLeft (8);

    auto buttons = left.removeFromBottom (28);
    addFolderButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (0, 1).withTrimmedRight (3));
    removeFolderButton.setBounds (buttons.reduced (0, 1).withTrimmedLeft (3));
    left.removeFromBottom (4);
    statusLabel.setBounds (left.removeFromBottom (20));
    sourceList.setBounds (left);

    auto top = area.removeFromTop (30);
    searchBox.setBounds (top);
    automixButton.setBounds (top.removeFromLeft (84).reduced (0, 1));
    top.removeFromLeft (4);
    for (auto* b : { &mixNowButton, &skipButton, &removeButton, &clearButton })
    {
        b->setBounds (top.removeFromLeft (b == &mixNowButton ? 84 : 62).reduced (0, 1));
        top.removeFromLeft (4);
    }
    fadeBox.setBounds (top.removeFromLeft (112).reduced (0, 2));
    top.removeFromLeft (8);
    automixStatus.setBounds (top);
    area.removeFromTop (6);
    table.setBounds (area);
}
}
