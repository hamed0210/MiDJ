#include "UI/LibraryBrowser.h"
#include "UI/DarkLookAndFeel.h"

#include <map>

namespace midj
{
namespace
{
    juce::String formatDuration (double seconds)
    {
        const int total = (int) std::round (seconds);
        return juce::String::formatted ("%d:%02d", total / 60, total % 60);
    }

    // "D:\Musica\Salsa" -> {"D:", "Musica", "Salsa"}
    juce::StringArray splitPath (const juce::String& path)
    {
        juce::StringArray parts;
        parts.addTokens (path, "\\/", "");
        parts.removeEmptyStrings();
        return parts;
    }
}

// --- Nodo del arbol ----------------------------------------------------------------------------

class LibraryBrowser::Node : public juce::TreeViewItem
{
public:
    enum class Kind { root, all, audio, video, favorites, mostPlayed, mine, myFolder, disks, drive, folder };

    Node (LibraryBrowser& o, Kind k, const juce::String& l, const juce::File& f, int c)
        : owner (o), kind (k), label (l), file (f), count (c) {}

    bool mightContainSubItems() override { return getNumSubItems() > 0; }
    juce::String getUniqueName() const override { return juce::String ((int) kind) + "|" + file.getFullPathName() + "|" + label; }
    int getItemHeight() const override { return 30; }
    bool canBeSelected() const override { return kind != Kind::root; }
    bool removable() const { return kind == Kind::myFolder || kind == Kind::drive || kind == Kind::folder; }

    void paintOpenCloseButton (juce::Graphics& g, const juce::Rectangle<float>& area, juce::Colour, bool) override
    {
        juce::Graphics::ScopedSaveState s (g);
        const auto c = area.getCentre();
        g.addTransform (juce::AffineTransform::rotation (isOpen() ? juce::MathConstants<float>::halfPi : 0.0f, c.x, c.y));
        Icons::draw (g, area.withSizeKeepingCentre (10.0f, 10.0f), Icons::Glyph::chevron, DarkLookAndFeel::textQuiet);
    }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.0f, 1.0f);
        if (isSelected())
        {
            g.setColour (DarkLookAndFeel::selection);
            g.fillRoundedRectangle (r, 6.0f);
        }
        r.removeFromLeft (2.0f);
        // Icono: equipo, nota, video, estrella, disco o carpeta.
        const auto icon = r.removeFromLeft (18.0f).withSizeKeepingCentre (16.0f, 16.0f);
        switch (kind)
        {
            case Kind::all: case Kind::disks: Icons::draw (g, icon, Icons::Glyph::computer, DarkLookAndFeel::textQuiet); break;
            case Kind::audio:   Icons::draw (g, icon, Icons::Glyph::note, DarkLookAndFeel::accentA); break;
            case Kind::video:   Icons::draw (g, icon, Icons::Glyph::film, juce::Colour (0xffd58cf5)); break;
            case Kind::favorites:  Icons::draw (g, icon, Icons::Glyph::star, DarkLookAndFeel::accentB); break;
            case Kind::mostPlayed: Icons::draw (g, icon, Icons::Glyph::play, DarkLookAndFeel::good); break;
            case Kind::mine:    Icons::draw (g, icon, Icons::Glyph::folderPlus, DarkLookAndFeel::textQuiet); break;
            case Kind::drive:   Icons::draw (g, icon, Icons::Glyph::drive, DarkLookAndFeel::textQuiet); break;
            case Kind::myFolder: case Kind::folder: Icons::draw (g, icon, Icons::Glyph::folder, juce::Colour (0xffc9c9cf)); break;
            case Kind::root: break;
        }
        r.removeFromLeft (8.0f);
        if (removable())
        {
            // La x para quitarla de la biblioteca (clic: ver itemClicked).
            Icons::draw (g, r.removeFromRight (22.0f).withSizeKeepingCentre (10.0f, 10.0f), Icons::Glyph::close, DarkLookAndFeel::textFaint);
            r.removeFromRight (2.0f);
        }
        if (count >= 0)
        {
            g.setColour (DarkLookAndFeel::textFaint);
            g.setFont (DarkLookAndFeel::mono (11.0f));
            const auto text = juce::String (count);
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 6.0f;
            g.drawText (text, r.removeFromRight (w), juce::Justification::centredRight, false);
        }
        g.setColour (isSelected() ? DarkLookAndFeel::text : juce::Colour (0xffc9c9cf));
        g.setFont (DarkLookAndFeel::sans (13.0f, isSelected()));
        g.drawText (label, r, juce::Justification::centredLeft, true);
    }

    void itemClicked (const juce::MouseEvent& e) override
    {
        if (removable() && e.x >= getItemWidth() - 26)
            owner.removeNode (*this);
    }

    void itemSelectionChanged (bool isNowSelected) override
    {
        if (isNowSelected)
            owner.selectNode (this);
    }

    juce::String getTooltip() override
    {
        if (removable())
            return file.getFullPathName() + "\n(x: quitar de la biblioteca, sin borrar archivos)";
        return {};
    }

    LibraryBrowser& owner;
    Kind kind;
    juce::String label;
    juce::File file;
    int count;
};

// --- Botones de cada fila: A, B y + -------------------------------------------------------------

class LibraryBrowser::RowButtons : public juce::Component
{
public:
    explicit RowButtons (LibraryBrowser& o) : owner (o)
    {
        for (auto [button, deck] : { std::pair<juce::TextButton*, int> { &a, 0 }, std::pair<juce::TextButton*, int> { &b, 1 } })
        {
            button->setColour (juce::TextButton::textColourOffId, DarkLookAndFeel::deckColour (deck));
            button->setTooltip (juce::String ("Cargar en el deck ") + (deck == 0 ? "A" : "B"));
            button->onClick = [this, deck]
            {
                if (juce::isPositiveAndBelow (row, (int) owner.tracks.size()) && owner.onLoadToDeck)
                    owner.onLoadToDeck (owner.tracks[(size_t) row], deck);
            };
            addAndMakeVisible (button);
        }
        plus.setBordered (true);
        plus.onClick = [this]
        {
            if (juce::isPositiveAndBelow (row, (int) owner.tracks.size()) && owner.automix != nullptr)
                owner.automix->add (itemFor (owner.tracks[(size_t) row]));
        };
        addAndMakeVisible (plus);
        for (auto* c : getChildren())
        {
            c->setMouseClickGrabsKeyboardFocus (false);
            c->setWantsKeyboardFocus (false);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().withSizeKeepingCentre (getWidth(), juce::jmin (getHeight() - 6, 28));
        const int w = (r.getWidth() - 8) / 3;
        a.setBounds (r.removeFromLeft (w));
        r.removeFromLeft (4);
        b.setBounds (r.removeFromLeft (w));
        r.removeFromLeft (4);
        plus.setBounds (r);
    }

    LibraryBrowser& owner;
    int row = -1;
    juce::TextButton a { "A" }, b { "B" };
    IconButton plus { Icons::Glyph::plus, "Anadir al automix" };
};

// --- Sincronizar con el PC (en un hilo) ---------------------------------------------------------

class LibraryBrowser::SyncJob : public juce::Thread
{
public:
    SyncJob (LibraryBrowser& o, Library::SyncOptions opts) : juce::Thread ("MiDJ sincronizar"), owner (&o), options (std::move (opts)) {}

    void run() override
    {
        int files = 0;
        auto found = Library::findMediaFolders (options,
            [this] (const juce::File& dir, int n)
            {
                const juce::ScopedLock sl (lock);
                current = dir.getFullPathName();
                filesSoFar = n;
            },
            [this] { return threadShouldExit(); }, files);
        const bool stopped = threadShouldExit();
        auto target = owner;
        juce::MessageManager::callAsync ([target, found, files, stopped]
        {
            if (target != nullptr)
                target->finishSync (found, files, stopped);
        });
    }

    juce::String getCurrent() const { const juce::ScopedLock sl (lock); return current; }
    int getFiles() const { const juce::ScopedLock sl (lock); return filesSoFar; }

private:
    juce::Component::SafePointer<LibraryBrowser> owner;
    Library::SyncOptions options;
    juce::CriticalSection lock;
    juce::String current;
    int filesSoFar = 0;
};

// --- Navegador ---------------------------------------------------------------------------------

LibraryBrowser::LibraryBrowser (Library& l) : library (l)
{
    library.addChangeListener (this);

    tree.setRootItemVisible (false);
    tree.setIndentSize (14);
    tree.setDefaultOpenness (false);
    tree.setColour (juce::TreeView::backgroundColourId, juce::Colours::transparentBlack);
    tree.setColour (juce::TreeView::selectedItemBackgroundColourId, juce::Colours::transparentBlack);
    tree.setColour (juce::TreeView::linesColourId, juce::Colours::transparentBlack);
    tree.setColour (juce::TreeView::dragAndDropIndicatorColourId, DarkLookAndFeel::accentA);
    addAndMakeVisible (tree);

    syncButton.setBordered (true);
    syncButton.onClick = [this] { showSyncOptions(); };
    addFolderButton.setBordered (true);
    addFolderButton.onClick = [this] { chooseFolder(); };
    addAndMakeVisible (syncButton);
    addAndMakeVisible (addFolderButton);
    stopSyncButton.onClick = [this] { if (syncJob != nullptr) syncJob->signalThreadShouldExit(); };
    addChildComponent (stopSyncButton);

    toastLabel.setFont (DarkLookAndFeel::sans (12.0f));
    toastLabel.setColour (juce::Label::textColourId, juce::Colour (0xffc9c9cf));
    toastLabel.setJustificationType (juce::Justification::topLeft);
    toastLabel.setMinimumHorizontalScale (1.0f);
    undoButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff7cc4ff));
    undoButton.onClick = [this]
    {
        if (lastRemoval)
        {
            library.undoRemoval (*lastRemoval);
            library.startBackgroundWork();
            lastRemoval.reset();
            showToast ("Restaurado.", false);
        }
    };
    closeToastButton.onClick = [this] { showToast ({}, false); };
    addChildComponent (toastLabel);
    addChildComponent (undoButton);
    addChildComponent (closeToastButton);

    searchBox.setTextToShowWhenEmpty ("Buscar titulo, artista, album o genero   (Ctrl+F)", DarkLookAndFeel::textQuiet);
    searchBox.setColour (juce::TextEditor::backgroundColourId, DarkLookAndFeel::background);
    searchBox.setColour (juce::TextEditor::outlineColourId, DarkLookAndFeel::line);
    searchBox.setFont (DarkLookAndFeel::sans (14.0f));
    searchBox.setIndents (10, 8);
    searchBox.onTextChange = [this] { startTimer (200); };   // espera a que se deje de teclear
    searchBox.onEscapeKey = [this] { searchBox.clear(); refresh(); unfocusAllComponents(); };
    searchBox.onReturnKey = [this] { unfocusAllComponents(); };   // Intro: a la lista (las flechas y el espacio vuelven a ser atajos)
    addAndMakeVisible (searchBox);

    auto& header = table.getHeader();
    const int columnFlags = juce::TableHeaderComponent::defaultFlags;
    header.addColumn ({}, typeCol, 40, 40, 40, juce::TableHeaderComponent::notResizableOrSortable);
    header.addColumn ({}, favCol, 30, 30, 30, juce::TableHeaderComponent::notResizableOrSortable);
    header.addColumn ("TITULO", titleCol, 280, 100, -1, columnFlags);
    header.addColumn ("ARTISTA", artistCol, 190, 70, -1, columnFlags);
    header.addColumn ("BPM", bpmCol, 64, 50, 90, columnFlags);
    header.addColumn ("DUR.", durationCol, 60, 50, 90, columnFlags);
    header.addColumn ("CARGAR", loadCol, 116, 116, 116, juce::TableHeaderComponent::notResizableOrSortable);
    header.setSortColumnId (artistCol, true);
    header.setStretchToFitActive (true);
    header.setColour (juce::TableHeaderComponent::backgroundColourId, DarkLookAndFeel::panel);
    header.setColour (juce::TableHeaderComponent::textColourId, DarkLookAndFeel::textQuiet);
    header.setColour (juce::TableHeaderComponent::outlineColourId, juce::Colours::transparentBlack);
    header.setColour (juce::TableHeaderComponent::highlightColourId, DarkLookAndFeel::selection);
    table.setHeaderHeight (26);
    table.setRowHeight (38);
    table.setMultipleSelectionEnabled (false);
    table.setColour (juce::ListBox::backgroundColourId, DarkLookAndFeel::panel);
    addAndMakeVisible (table);

    // Barrita entre el arbol y la lista.
    treeHandle.onStart = [this] { dragStartWidth = treeWidth; };
    treeHandle.onDrag = [this] (int dx) { setFoldersWidth (dragStartWidth + dx); };
    treeHandle.onReset = [this] { setFoldersWidth (defaultTreeWidth); };
    addAndMakeVisible (treeHandle);

    rebuildTree (true);
    refresh();
}

LibraryBrowser::~LibraryBrowser()
{
    if (syncJob != nullptr)
    {
        syncJob->signalThreadShouldExit();
        syncJob->stopThread (10000);
    }
    tree.setRootItem (nullptr);
    library.removeChangeListener (this);
}

void LibraryBrowser::addLearnTargets (std::vector<LearnTarget>& out)
{
    out.push_back ({ &table, "browser.scroll" });
}

juce::String LibraryBrowser::selectionName() const
{
    return filterLabel;
}

void LibraryBrowser::rebuildTree (bool force)
{
    const auto paths = library.getTrackPaths();
    const auto folders = library.getFolders();
    // Firma: si no cambio ni una ruta ni una carpeta, el arbol se queda como esta.
    juce::int64 hash = 0;
    for (const auto& p : paths)
        hash += p.hashCode64();
    const int favorites = library.countFavorites(), played = library.countPlayed();
    juce::String signature = juce::String (paths.size()) + "|" + juce::String (hash) + "|" + juce::String (favorites) + "|" + juce::String (played);
    for (const auto& f : folders)
        signature << "|" << f.getFullPathName();
    if (! force && signature == treeSignature)
        return;
    treeSignature = signature;

    // Se guarda lo abierto y lo elegido para dejarlo igual.
    std::unique_ptr<juce::XmlElement> openness = rootNode != nullptr ? tree.getOpennessState (true) : nullptr;
    tree.setRootItem (nullptr);
    rootNode = std::make_unique<Node> (*this, Node::Kind::root, juce::String(), juce::File(), -1);

    int audio = 0, video = 0;
    for (const auto& p : paths)
        (Library::isVideoFile (juce::File (p)) ? video : audio)++;
    auto* local = new Node (*this, Node::Kind::all, "Musica local", {}, paths.size());
    local->addSubItem (new Node (*this, Node::Kind::audio, "Musica", {}, audio));
    local->addSubItem (new Node (*this, Node::Kind::video, "Videos", {}, video));
    rootNode->addSubItem (local);
    rootNode->addSubItem (new Node (*this, Node::Kind::favorites, "Favoritos", {}, favorites));
    rootNode->addSubItem (new Node (*this, Node::Kind::mostPlayed, "Mas reproducidos", {}, played));

    auto countUnder = [&paths] (const juce::File& folder)
    {
        const auto prefix = folder.getFullPathName().trimCharactersAtEnd ("\\") + "\\";
        int n = 0;
        for (const auto& p : paths)
            if (p.startsWithIgnoreCase (prefix))
                ++n;
        return n;
    };
    auto* mine = new Node (*this, Node::Kind::mine, "Mis carpetas", {}, -1);
    for (const auto& f : folders)
        mine->addSubItem (new Node (*this, Node::Kind::myFolder, f.getFileName().isNotEmpty() ? f.getFileName() : f.getFullPathName(), f, countUnder (f)));
    rootNode->addSubItem (mine);

    // Discos: solo las carpetas donde hay canciones (y las que llevan a ellas).
    struct Dir
    {
        std::map<juce::String, Dir> sub;
        int count = 0;
    };
    std::map<juce::String, Dir> drives;
    for (const auto& p : paths)
    {
        auto parts = splitPath (p);
        if (parts.size() < 2)
            continue;
        parts.remove (parts.size() - 1);   // el archivo
        auto* d = &drives[parts[0].toUpperCase()];
        ++d->count;
        for (int i = 1; i < parts.size(); ++i)
        {
            d = &d->sub[parts[i]];
            ++d->count;
        }
    }
    auto* disks = new Node (*this, Node::Kind::disks, "Discos", {}, -1);
    std::function<void (Node*, const Dir&, const juce::String&)> addDirs = [&] (Node* parent, const Dir& dir, const juce::String& path)
    {
        for (const auto& [name, child] : dir.sub)
        {
            const auto childPath = path + "\\" + name;
            auto* node = new Node (*this, Node::Kind::folder, name, juce::File (childPath), child.count);
            parent->addSubItem (node);
            addDirs (node, child, childPath);
        }
    };
    for (const auto& [letter, dir] : drives)
    {
        const juce::File drive (letter + "\\");
        auto* node = new Node (*this, Node::Kind::drive, Library::driveName (drive), drive, dir.count);
        disks->addSubItem (node);
        addDirs (node, dir, letter);
    }
    rootNode->addSubItem (disks);

    tree.setRootItem (rootNode.get());
    if (openness != nullptr)
        tree.restoreOpennessState (*openness, false);
    else
    {
        local->setOpen (true);
        mine->setOpen (true);
        disks->setOpen (true);
        for (int i = 0; i < disks->getNumSubItems(); ++i)
            disks->getSubItem (i)->setOpen (true);
    }

    // Lo elegido: el mismo nodo si sigue; si no (se quito), Musica local.
    Node* selected = nullptr;
    std::function<void (Node*)> find = [&] (Node* n)
    {
        for (int i = 0; i < n->getNumSubItems() && selected == nullptr; ++i)
        {
            auto* c = static_cast<Node*> (n->getSubItem (i));
            const bool match = c->kind == Node::Kind::all ? filterFolder == juce::File() && filterKind == 0 && filterLabel == c->label
                             : c->kind == Node::Kind::audio ? filterKind == 1
                             : c->kind == Node::Kind::video ? filterKind == 2
                             : c->kind == Node::Kind::favorites ? filterKind == 3
                             : c->kind == Node::Kind::mostPlayed ? filterKind == 4
                             : (c->kind == Node::Kind::myFolder || c->kind == Node::Kind::drive || c->kind == Node::Kind::folder)
                                   && filterFolder != juce::File() && c->file == filterFolder;
            if (match)
                selected = c;
            else
                find (c);
        }
    };
    find (rootNode.get());
    if (selected != nullptr)
        selected->setSelected (true, true, juce::dontSendNotification);
    else
        local->setSelected (true, true);   // vuelve a Musica local (y refresca la lista)
}

void LibraryBrowser::selectNode (Node* node)
{
    filterFolder = {};
    filterKind = 0;
    filterLabel = node->label;
    switch (node->kind)
    {
        case Node::Kind::audio: filterKind = 1; break;
        case Node::Kind::video: filterKind = 2; break;
        case Node::Kind::favorites: filterKind = 3; break;
        case Node::Kind::mostPlayed: filterKind = 4; break;
        case Node::Kind::myFolder: case Node::Kind::drive: case Node::Kind::folder: filterFolder = node->file; break;
        default: break;
    }
    refresh();
}

void LibraryBrowser::removeNode (Node& node)
{
    const auto label = node.label;
    const int count = node.count;
    lastRemoval = library.removeFromLibrary (node.file);
    showToast ("Se quito \"" + label + "\"" + (count > 0 ? " (" + juce::String (count) + (count == 1 ? " archivo)" : " archivos)") : juce::String())
                   + ". Los archivos siguen en el disco.", true);
}

void LibraryBrowser::showToast (const juce::String& text, bool canUndo)
{
    if (! canUndo)
        lastRemoval.reset();
    toastLabel.setText (text, juce::dontSendNotification);
    const bool show = text.isNotEmpty();
    toastLabel.setVisible (show);
    closeToastButton.setVisible (show);
    undoButton.setVisible (show && canUndo);
    toastShownAt = juce::Time::getMillisecondCounter();
    resized();
    repaint();
}

void LibraryBrowser::showSyncOptions()
{
    if (syncJob != nullptr)
        return;
    // Ventanita: en que discos buscar y que tipos de archivo.
    class Options : public juce::Component
    {
    public:
        explicit Options (LibraryBrowser& o) : owner (o)
        {
            for (const auto& d : Library::localDrives())
            {
                auto* t = drives.add (new juce::ToggleButton (Library::driveName (d)));
                t->setToggleState (true, juce::dontSendNotification);
                t->getProperties().set ("path", d.getFullPathName());
                t->onClick = [this] { update(); };
                addAndMakeVisible (t);
            }
            for (auto* t : { &audio, &video })
            {
                t->setToggleState (true, juce::dontSendNotification);
                t->onClick = [this] { update(); };
                addAndMakeVisible (t);
            }
            cancel.onClick = [this] { close(); };
            start.getProperties().set (DarkLookAndFeel::filledProperty, true);
            start.setColour (juce::TextButton::buttonColourId, DarkLookAndFeel::accentA);
            start.setColour (juce::TextButton::textColourOffId, DarkLookAndFeel::background);
            start.onClick = [this]
            {
                Library::SyncOptions opts;
                for (auto* t : drives)
                    if (t->getToggleState())
                        opts.roots.add (juce::File (t->getProperties()["path"].toString()));
                opts.audio = audio.getToggleState();
                opts.video = video.getToggleState();
                auto& o = owner;
                close();
                o.startSync (opts);
            };
            addAndMakeVisible (cancel);
            addAndMakeVisible (start);
            setSize (320, 150 + 38 * (drives.size() + 2));
            update();
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (DarkLookAndFeel::text);
            g.setFont (DarkLookAndFeel::sans (15.0f, true));
            g.drawText ("Buscar musica y videos en el PC", 16, 12, getWidth() - 32, 22, juce::Justification::centredLeft);
            g.setColour (DarkLookAndFeel::textQuiet);
            g.setFont (DarkLookAndFeel::sans (11.0f, true).withKerningFactor (0.08f));
            g.drawText ("DISCOS", 16, 42, 200, 16, juce::Justification::centredLeft);
            g.drawText ("TIPOS DE ARCHIVO", 16, 70 + 38 * drives.size(), 200, 16, juce::Justification::centredLeft);
            g.setFont (DarkLookAndFeel::sans (11.0f));
            g.drawFittedText ("Se saltan Windows, Archivos de programa y AppData. Lo nuevo se analiza en segundo plano.",
                              16, getHeight() - 92, getWidth() - 32, 30, juce::Justification::topLeft, 2);
        }

        void resized() override
        {
            int y = 60;
            for (auto* t : drives)
            {
                t->setBounds (16, y, getWidth() - 32, 34);
                y += 38;
            }
            y += 28;
            audio.setBounds (16, y, getWidth() - 32, 34);
            video.setBounds (16, y + 38, getWidth() - 32, 34);
            auto buttons = juce::Rectangle<int> (16, getHeight() - 50, getWidth() - 32, 34);
            start.setBounds (buttons.removeFromRight (150));
            buttons.removeFromRight (8);
            cancel.setBounds (buttons.removeFromRight (90));
        }

    private:
        void update()
        {
            bool anyDrive = false;
            for (auto* t : drives)
                anyDrive = anyDrive || t->getToggleState();
            start.setEnabled (anyDrive && (audio.getToggleState() || video.getToggleState()));
        }
        void close()
        {
            if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                box->dismiss();
        }

        LibraryBrowser& owner;
        juce::OwnedArray<juce::ToggleButton> drives;
        juce::ToggleButton audio { "Musica  (mp3, wav, flac, m4a...)" }, video { "Video  (mp4, mov, m4v, wmv)" };
        juce::TextButton cancel { "Cancelar" }, start { "Buscar y anadir" };
    };

    juce::CallOutBox::launchAsynchronously (std::make_unique<Options> (*this), syncButton.getScreenBounds(), nullptr);
}

void LibraryBrowser::startSync (const Library::SyncOptions& options)
{
    if (syncJob != nullptr || options.roots.isEmpty())
        return;
    showToast ({}, false);
    syncJob = std::make_unique<SyncJob> (*this, options);
    syncJob->startThread (juce::Thread::Priority::low);
    syncButton.setEnabled (false);
    stopSyncButton.setVisible (true);
    startTimer (100);
    resized();
    repaint();
}

void LibraryBrowser::finishSync (juce::Array<juce::File> folders, int files, bool stopped)
{
    if (syncJob != nullptr)
    {
        syncJob->stopThread (5000);
        syncJob.reset();
    }
    syncButton.setEnabled (true);
    stopSyncButton.setVisible (false);
    if (stopped)
    {
        showToast ("Busqueda detenida: no se anadio nada.", false);
        return;
    }
    // Solo las carpetas que no estaban ya en la biblioteca (ni dentro de otra).
    const auto existing = library.getFolders();
    int added = 0;
    for (const auto& f : folders)
    {
        const bool covered = std::any_of (existing.begin(), existing.end(), [&] (const juce::File& e) { return f == e || f.isAChildOf (e); });
        if (! covered)
        {
            library.addFolder (f);
            ++added;
        }
    }
    if (added > 0)
        library.startBackgroundWork();
    showToast (added > 0 ? "Listo: " + juce::String (added) + (added == 1 ? " carpeta nueva" : " carpetas nuevas") + " con musica o video ("
                               + juce::String (files) + " archivos encontrados). Se anaden y analizan en segundo plano."
                         : "Listo: no hay carpetas nuevas (" + juce::String (files) + " archivos ya estaban).",
               false);
}

void LibraryBrowser::refresh()
{
    // Se conserva la cancion elegida (por id) aunque la lista cambie.
    const int row = table.getSelectedRow();
    const auto selectedId = row >= 0 && row < (int) tracks.size() ? tracks[(size_t) row].id : 0;

    if (filterKind == 3 || filterKind == 4)
    {
        // Favoritos o mas reproducidos (en su orden), con el texto del buscador si hay.
        tracks = filterKind == 3 ? library.getFavorites() : library.getMostPlayed();
        juce::StringArray words;
        words.addTokens (Library::normalise (searchBox.getText()), " \t", "\"");
        words.removeEmptyStrings();
        std::erase_if (tracks, [&words] (const TrackInfo& t)
        {
            const auto text = Library::normalise (t.title + " " + t.artist + " " + t.album + " " + t.genre + " " + t.getFile().getFileName());
            for (const auto& w : words)
                if (! text.contains (w))
                    return true;
            return false;
        });
    }
    else
        tracks = library.search (searchBox.getText(), filterFolder, sort, ascending);
    if (filterKind == 1 || filterKind == 2)
        tracks.erase (std::remove_if (tracks.begin(), tracks.end(),
                                      [this] (const TrackInfo& t) { return Library::isVideoFile (t.getFile()) != (filterKind == 2); }),
                      tracks.end());
    table.updateContent();
    table.repaint();
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == selectedId)
            table.selectRow ((int) i, true, true);
    repaint (footerArea);
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
    chooser = std::make_unique<juce::FileChooser> ("Elige una carpeta de musica o videos",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();
                              if (! folder.isDirectory())
                                  return;
                              library.addFolder (folder);
                              library.startBackgroundWork();
                          });
}

void LibraryBrowser::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // La biblioteca avisa a menudo mientras escanea o analiza: se agrupan los avisos.
    if (! isTimerRunning())
        startTimer (400);
}

void LibraryBrowser::timerCallback()
{
    if (syncJob != nullptr)
    {
        repaint (syncArea);   // carpeta actual, archivos encontrados y la barra
        return;               // el temporizador sigue a 100 ms mientras busca
    }
    stopTimer();
    rebuildTree (false);
    refresh();
    // El aviso se va solo a los 15 s.
    if (toastLabel.isVisible() && juce::Time::getMillisecondCounter() - toastShownAt > 15000)
        showToast ({}, false);
    else if (toastLabel.isVisible())
        startTimer (1000);
}

void LibraryBrowser::paintRowBackground (juce::Graphics& g, int row, int width, int height, bool selected)
{
    if (selected)
    {
        g.setColour (DarkLookAndFeel::selection);
        g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 1.0f, (float) width, (float) height - 2.0f), 6.0f);
    }
    else if (row > 0)
    {
        g.setColour (DarkLookAndFeel::selection.withAlpha (0.6f));
        g.drawHorizontalLine (0, 8.0f, (float) width - 8.0f);
    }
}

void LibraryBrowser::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= (int) tracks.size())
        return;
    const auto& t = tracks[(size_t) row];
    if (column == typeCol)
    {
        // Video o audio en una pastilla.
        const auto chip = juce::Rectangle<float> (6.0f, (float) height / 2.0f - 12.0f, 30.0f, 24.0f);
        g.setColour (DarkLookAndFeel::raised);
        g.fillRoundedRectangle (chip, 5.0f);
        const bool video = Library::isVideoFile (t.getFile());
        Icons::draw (g, chip.reduced (7.0f, 5.0f), video ? Icons::Glyph::film : Icons::Glyph::note,
                     video ? juce::Colour (0xffd58cf5) : DarkLookAndFeel::textQuiet);
        return;
    }
    if (column == favCol)
    {
        // Estrella: encendida si es favorita (clic: cambiar).
        Icons::draw (g, juce::Rectangle<float> ((float) width, (float) height).withSizeKeepingCentre (15.0f, 15.0f), Icons::Glyph::star,
                     t.favorite ? DarkLookAndFeel::accentB : DarkLookAndFeel::textFaint.withAlpha (0.5f));
        return;
    }
    juce::String text;
    auto justification = juce::Justification::centredLeft;
    g.setColour (DarkLookAndFeel::textQuiet);
    g.setFont (DarkLookAndFeel::sans (13.0f));
    switch (column)
    {
        case titleCol:
            text = t.title.isNotEmpty() ? t.title : t.getFile().getFileNameWithoutExtension();
            g.setColour (DarkLookAndFeel::text);
            g.setFont (DarkLookAndFeel::sans (13.0f, true));
            break;
        case artistCol:   text = t.artist; break;
        case durationCol:
            text = formatDuration (t.duration);
            justification = juce::Justification::centredRight;
            g.setFont (DarkLookAndFeel::mono (12.0f));
            break;
        case bpmCol:
            justification = juce::Justification::centredRight;
            g.setFont (DarkLookAndFeel::mono (12.0f));
            if (t.analysed && t.bpm > 0.0)
            {
                text = juce::String (t.bpm, 1);
                g.setColour (DarkLookAndFeel::text);
            }
            else
                text = juce::String::fromUTF8 ("\xe2\x80\x94");   // aun sin analizar
            break;
        default: break;
    }
    g.drawText (text, 6, 0, width - 12, height, justification, true);
}

juce::Component* LibraryBrowser::refreshComponentForCell (int row, int column, bool, juce::Component* existing)
{
    if (column != loadCol)
    {
        delete existing;
        return nullptr;
    }
    auto* buttons = dynamic_cast<RowButtons*> (existing);
    if (buttons == nullptr)
    {
        delete existing;
        buttons = new RowButtons (*this);
    }
    buttons->row = row;
    return buttons;
}

void LibraryBrowser::sortOrderChanged (int columnId, bool forwards)
{
    switch (columnId)
    {
        case titleCol:    sort = Library::SortColumn::title; break;
        case artistCol:   sort = Library::SortColumn::artist; break;
        case bpmCol:      sort = Library::SortColumn::bpm; break;
        case durationCol: sort = Library::SortColumn::duration; break;
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

void LibraryBrowser::cellClicked (int row, int column, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) tracks.size())
        return;
    if (e.mods.isPopupMenu())
    {
        table.selectRow (row);
        showTrackMenu (row);
    }
    else if (column == favCol)
        toggleFavorite (tracks[(size_t) row]);
}

void LibraryBrowser::toggleFavorite (const TrackInfo& track)
{
    library.setFavorite (track.getFile(), ! track.favorite);
    rebuildTree (false);   // el numero de Favoritos
    refresh();
}

void LibraryBrowser::showTrackMenu (int row)
{
    enum { addToAutomix = 1, playNext, loadA, loadB, prepareStems, favorite };
    juce::PopupMenu menu;
    menu.addItem (favorite, tracks[(size_t) row].favorite ? "Quitar de favoritos" : "Agregar a favoritos");
    menu.addSeparator();
    if (automix != nullptr)
    {
        menu.addItem (addToAutomix, "Agregar a automix");
        menu.addItem (playNext, "Agregar a automix como la siguiente");
        menu.addSeparator();
    }
    menu.addItem (loadA, "Cargar en el deck A");
    menu.addItem (loadB, "Cargar en el deck B");
    if (onPrepareStems)
    {
        menu.addSeparator();
        menu.addItem (prepareStems, "Preparar stems (voz / instrumental) en segundo plano");
    }
    const auto track = tracks[(size_t) row];
    menu.showMenuAsync (juce::PopupMenu::Options(), [this, track] (int r)
    {
        if (r == addToAutomix && automix != nullptr)          automix->add (itemFor (track));
        else if (r == playNext && automix != nullptr)         automix->insertNext (itemFor (track));
        else if ((r == loadA || r == loadB) && onLoadToDeck)  onLoadToDeck (track, r == loadA ? 0 : 1);
        else if (r == prepareStems && onPrepareStems)          onPrepareStems (track);
        else if (r == favorite)                                toggleFavorite (track);
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
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 14.0f);
    g.setColour (DarkLookAndFeel::text);
    g.setFont (DarkLookAndFeel::sans (14.0f, true));
    g.drawText ("Carpetas", treeHeaderArea, juce::Justification::centredLeft, false);

    if (syncJob != nullptr && ! syncArea.isEmpty())
    {
        auto r = syncArea;
        g.setColour (DarkLookAndFeel::raised);
        g.fillRoundedRectangle (r.toFloat(), 10.0f);
        r.reduce (10, 8);
        g.setColour (DarkLookAndFeel::text);
        g.setFont (DarkLookAndFeel::sans (13.0f, true));
        g.drawText ("Sincronizando...", r.removeFromTop (18), juce::Justification::centredLeft, false);
        r.removeFromTop (6);
        // Barra sin porcentaje (no se sabe cuanto falta): un tramo que va y viene.
        auto bar = r.removeFromTop (6).toFloat();
        g.setColour (DarkLookAndFeel::background);
        g.fillRoundedRectangle (bar, 3.0f);
        const float t = (float) std::fmod (juce::Time::getMillisecondCounter() / 1400.0, 1.0);
        const float x = bar.getX() + (bar.getWidth() * 0.7f) * (0.5f - 0.5f * std::cos (t * juce::MathConstants<float>::twoPi));
        g.setColour (DarkLookAndFeel::accentA);
        g.fillRoundedRectangle (bar.withX (x).withWidth (bar.getWidth() * 0.3f), 3.0f);
        r.removeFromTop (6);
        g.setColour (DarkLookAndFeel::textQuiet);
        g.setFont (DarkLookAndFeel::mono (11.0f));
        g.drawText (syncJob->getCurrent(), r.removeFromTop (16), juce::Justification::centredLeft, true);
        g.setColour (DarkLookAndFeel::text);
        g.setFont (DarkLookAndFeel::sans (12.0f));
        const int n = syncJob->getFiles();
        g.drawText (juce::String (n) + (n == 1 ? " archivo encontrado" : " archivos encontrados"), r.removeFromTop (22),
                    juce::Justification::centredLeft, false);
    }
    if (toastLabel.isVisible())
    {
        g.setColour (DarkLookAndFeel::raised);
        g.fillRoundedRectangle (toastArea.toFloat(), 8.0f);
    }

    // Pie de la lista: cuantas hay y donde, y lo que hace la biblioteca (busqueda, analisis).
    g.setColour (DarkLookAndFeel::textQuiet);
    g.setFont (DarkLookAndFeel::sans (12.0f));
    const int n = (int) tracks.size();
    g.drawText (juce::String (n) + (n == 1 ? " archivo" : " archivos") + " en \"" + selectionName() + "\"" + juce::String::fromUTF8 ("  \xc2\xb7  ")
                    + "A / B cargan en el deck, + anade al automix",
                footerArea, juce::Justification::centredLeft, true);
    if (library.isBusy())
        g.drawText (library.getStatusText(), footerArea, juce::Justification::centredRight, false);
}

void LibraryBrowser::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto left = r.removeFromLeft (juce::jmin (treeWidth, r.getWidth() - 240));
    treeHandle.setBounds (r.removeFromLeft (12));

    // Arbol: cabecera con sincronizar y anadir carpeta, estado de la busqueda, arbol y aviso.
    treeHeaderArea = left.removeFromTop (32).withTrimmedLeft (4);
    auto header = treeHeaderArea;
    addFolderButton.setBounds (header.removeFromRight (32));
    header.removeFromRight (4);
    syncButton.setBounds (header.removeFromRight (32));
    left.removeFromTop (6);
    if (syncJob != nullptr)
    {
        syncArea = left.removeFromTop (112);
        stopSyncButton.setBounds (syncArea.getRight() - 84, syncArea.getBottom() - 34, 74, 26);
        left.removeFromTop (6);
    }
    else
        syncArea = {};
    if (toastLabel.isVisible())
    {
        toastArea = left.removeFromBottom (undoButton.isVisible() ? 92 : 64);
        auto t = toastArea.reduced (8, 6);
        auto buttons = t.removeFromBottom (26);
        closeToastButton.setBounds (t.removeFromRight (20).removeFromTop (20));
        toastLabel.setBounds (t);
        if (undoButton.isVisible())
            undoButton.setBounds (buttons.removeFromLeft (90));
        left.removeFromBottom (6);
    }
    tree.setBounds (left);

    // Lista: buscador, canciones y pie.
    searchBox.setBounds (r.removeFromTop (34));
    r.removeFromTop (8);
    footerArea = r.removeFromBottom (22);
    r.removeFromBottom (4);
    table.setBounds (r);
}

void LibraryBrowser::returnKeyPressed (int row)
{
    if (row >= 0 && row < (int) tracks.size() && onReturnKey)
        onReturnKey (tracks[(size_t) row]);
}
}
