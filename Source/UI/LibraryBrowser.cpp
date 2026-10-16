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

    refreshFolders();
    sourceList.selectRow (0);
    refresh();
}

LibraryBrowser::~LibraryBrowser()
{
    library.removeChangeListener (this);
}

void LibraryBrowser::SourceList::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (selected)
        g.fillAll (DarkLookAndFeel::accentA.withAlpha (0.35f));
    g.setColour (DarkLookAndFeel::text);
    g.setFont (juce::FontOptions (14.0f, row == 0 ? juce::Font::bold : juce::Font::plain));
    const auto name = row == 0 ? juce::String ("Toda la musica") : owner.folders[row - 1].getFileName();
    g.drawText (name, 8, 0, width - 12, height, juce::Justification::centredLeft, true);
}

juce::String LibraryBrowser::SourceList::getTooltipForRow (int row)
{
    return row == 0 ? juce::String ("Todas las canciones de todas las carpetas") : owner.folders[row - 1].getFullPathName();
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

    tracks = library.search (searchBox.getText(), selectedFolder(), sort, ascending);
    table.updateContent();
    table.repaint();
    removeFolderButton.setEnabled (sourceList.getSelectedRow() >= 1);

    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == selectedId)
            table.selectRow ((int) i, true, true);
    statusLabel.setText (library.getStatusText(), juce::dontSendNotification);
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

void LibraryBrowser::changeListenerCallback (juce::ChangeBroadcaster*)
{
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

    searchBox.setBounds (area.removeFromTop (30));
    area.removeFromTop (6);
    table.setBounds (area);
}
}
