#include "UI/SourceDialogs.h"
#include "UI/DarkLookAndFeel.h"

#include <map>

namespace midj
{
namespace
{
    // Selector de color que avisa en cada cambio (para la ventanita del boton Color...).
    class ColourPicker : public juce::ColourSelector, private juce::ChangeListener
    {
    public:
        ColourPicker (juce::Colour start, std::function<void (juce::Colour)> callback)
            : juce::ColourSelector (showAlphaChannel | showColourAtTop | showSliders | showColourspace), onChange (std::move (callback))
        {
            setCurrentColour (start, juce::dontSendNotification);
            addChangeListener (this);
            setSize (300, 300);
        }
        ~ColourPicker() override { removeChangeListener (this); }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { onChange (getCurrentColour()); }
        std::function<void (juce::Colour)> onChange;
    };

    const juce::Colour selectedRow (0xff2a5bd7);

    // Los tipos del dialogo, en el orden de OBS (y los propios de MiDJ al principio).
    const std::array<SourceType, 12> addableTypes { SourceType::decks, SourceType::deckA, SourceType::deckB, SourceType::youtube, SourceType::screen,
                                                    SourceType::window, SourceType::captureDevice, SourceType::image, SourceType::media,
                                                    SourceType::text, SourceType::colour, SourceType::visualizer };

    juce::String uniqueName (const SceneCollection& scenes, const juce::String& base)
    {
        auto taken = [&] (const juce::String& n)
        {
            for (const auto& s : scenes.scenes)
                for (const auto& i : s.items)
                    if (i.name == n)
                        return true;
            return false;
        };
        if (! taken (base))
            return base;
        for (int k = 2;; ++k)
            if (! taken (base + " " + juce::String (k)))
                return base + " " + juce::String (k);
    }

    juce::String formatTime (double seconds)
    {
        const int total = (int) std::max (0.0, seconds);
        return juce::String::formatted ("%d:%02d", total / 60, total % 60);
    }

    // Formatos de cada capturadora (preguntarlos abre el dispositivo: una vez por sesion).
    const std::vector<VideoCaptureReader::Format>& formatsOf (const juce::String& deviceId)
    {
        static std::map<juce::String, std::vector<VideoCaptureReader::Format>> cache;
        auto it = cache.find (deviceId);
        if (it == cache.end() || it->second.empty())
            it = cache.insert_or_assign (deviceId, VideoCaptureReader::formats (deviceId)).first;
        return it->second;
    }

    juce::String fpsText (double fps)
    {
        return std::abs (fps - std::round (fps)) < 0.01 ? juce::String ((int) std::round (fps)) : juce::String (fps, 2);
    }
}

// --- Anadir fuente -----------------------------------------------------------------------------

class AddSourceWindow::Content : public juce::Component
{
public:
    explicit Content (VideoStage& s) : stage (s)
    {
        types.setModel (&typeModel);
        types.setRowHeight (30);
        types.setColour (juce::ListBox::backgroundColourId, DarkLookAndFeel::background);
        existing.setModel (&existingModel);
        existing.setRowHeight (26);
        existing.setColour (juce::ListBox::backgroundColourId, DarkLookAndFeel::background);
        heading.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        hint.setText ("Elige que fuente anadir a la escena al aire.", juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
        empty.setJustificationType (juce::Justification::centred);
        empty.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
        empty.setInterceptsMouseClicks (false, false);
        makeVisible.setToggleState (true, juce::dontSendNotification);
        createButton.onClick = [this] { createNew(); };
        addExisting.onClick = [this] { addSelectedExisting(); };
        closeButton.onClick = [this] { stage.closeDialogs(); };
        for (auto* c : std::initializer_list<juce::Component*> { &types, &heading, &hint, &createButton, &existing, &empty,
                                                                   &addExisting, &makeVisible, &closeButton })
            addAndMakeVisible (c);
        // Multimedia, como la ultima que se suele anadir.
        types.selectRow ((int) (std::find (addableTypes.begin(), addableTypes.end(), SourceType::media) - addableTypes.begin()));
        setSize (860, 560);
    }

    ~Content() override
    {
        types.setModel (nullptr);
        existing.setModel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (DarkLookAndFeel::panel);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        types.setBounds (r.removeFromLeft (270));
        r.removeFromLeft (12);
        heading.setBounds (r.removeFromTop (26));
        hint.setBounds (r.removeFromTop (20));
        r.removeFromTop (8);
        auto bottom = r.removeFromBottom (32);
        makeVisible.setBounds (bottom.removeFromLeft (220));
        closeButton.setBounds (bottom.removeFromRight (100));
        r.removeFromBottom (8);
        createButton.setBounds (r.removeFromTop (60));
        r.removeFromTop (8);
        auto bar = r.removeFromBottom (30);
        addExisting.setBounds (bar.removeFromLeft (220));
        r.removeFromBottom (6);
        existing.setBounds (r);
        empty.setBounds (r);
    }

private:
    // "+ Crear nueva fuente" dentro de un recuadro de trazos, como en OBS.
    struct CreateButton : public juce::Button
    {
        CreateButton() : juce::Button ("Crear nueva fuente") {}
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.5f);
            if (over || down)
            {
                g.setColour (juce::Colours::white.withAlpha (down ? 0.12f : 0.06f));
                g.fillRoundedRectangle (r, 4.0f);
            }
            juce::Path outline, dashed;
            outline.addRoundedRectangle (r, 4.0f);
            const float dashes[] = { 6.0f, 4.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (DarkLookAndFeel::textQuiet);
            g.fillPath (dashed);
            g.setColour (DarkLookAndFeel::text);
            const auto label = r.withSizeKeepingCentre (220.0f, 24.0f);
            Icons::draw (g, label.withWidth (24.0f), Icons::Glyph::plus, DarkLookAndFeel::text);
            g.setFont (juce::FontOptions (15.0f));
            g.drawText ("Crear nueva fuente", label.withTrimmedLeft (30.0f), juce::Justification::centredLeft, false);
        }
    };

    struct TypeModel : juce::ListBoxModel
    {
        explicit TypeModel (Content& c) : owner (c) {}
        int getNumRows() override { return (int) addableTypes.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (! juce::isPositiveAndBelow (row, getNumRows()))
                return;
            auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
            if (selected)
            {
                g.setColour (selectedRow);
                g.fillRoundedRectangle (r.reduced (1.0f), 3.0f);
            }
            const auto type = addableTypes[(size_t) row];
            const auto ink = selected ? juce::Colours::white : DarkLookAndFeel::text;
            Icons::drawSource (g, r.removeFromLeft (34.0f).reduced (7.0f), type, ink);
            g.setColour (ink);
            g.setFont (juce::FontOptions (14.5f));
            g.drawText (SceneCollection::typeName (type), r, juce::Justification::centredLeft, true);
        }
        void selectedRowsChanged (int) override { owner.typeChanged(); }
        void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { owner.createNew(); }
        Content& owner;
    };

    struct ExistingModel : juce::ListBoxModel
    {
        explicit ExistingModel (Content& c) : owner (c) {}
        int getNumRows() override { return (int) owner.existingItems.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
        {
            if (! juce::isPositiveAndBelow (row, getNumRows()))
                return;
            if (selected)
                g.fillAll (selectedRow);
            g.setColour (selected ? juce::Colours::white : DarkLookAndFeel::text);
            g.setFont (juce::FontOptions (14.0f));
            g.drawText (owner.existingItems[(size_t) row].name, 10, 0, w - 20, h, juce::Justification::centredLeft, true);
        }
        void selectedRowsChanged (int row) override { owner.addExisting.setEnabled (row >= 0); }
        void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { owner.addSelectedExisting(); }
        Content& owner;
    };

    SourceType selectedType() const
    {
        return addableTypes[(size_t) juce::jlimit (0, (int) addableTypes.size() - 1, types.getSelectedRow())];
    }

    void typeChanged()
    {
        const auto type = selectedType();
        heading.setText (SceneCollection::typeName (type), juce::dontSendNotification);
        // Las fuentes de este tipo que ya hay en otras escenas (una por nombre).
        existingItems.clear();
        for (const auto& s : stage.getScenes().scenes)
            for (const auto& i : s.items)
                if (i.type == type && std::none_of (existingItems.begin(), existingItems.end(), [&i] (const SceneItem& e) { return e.name == i.name; }))
                    existingItems.push_back (i);
        existing.updateContent();
        existing.deselectAllRows();
        existing.repaint();
        addExisting.setEnabled (false);
        empty.setText (existingItems.empty() ? "No hay fuentes de " + SceneCollection::typeName (type) : juce::String(), juce::dontSendNotification);
    }

    void createNew()
    {
        auto& scenes = stage.getScenes();
        auto* scene = scenes.findScene (scenes.currentScene);
        if (scene == nullptr)
            return;
        const auto type = selectedType();
        auto item = scenes.makeItem (type, stage.getProgramWidth(), stage.getProgramHeight());
        item.name = uniqueName (scenes, SceneCollection::typeName (type));
        item.visible = makeVisible.getToggleState();
        if (type == SourceType::screen)
        {
            // La pantalla principal.
            const auto monitors = ScreenCapture::monitors();
            for (size_t m = 0; m < monitors.size(); ++m)
                if (monitors[m].primary)
                {
                    item.monitor = (int) m;
                    item.monitorId = monitors[m].id;
                }
        }
        if (type == SourceType::captureDevice)
        {
            // El primer dispositivo y la entrada de audio que parezca suya.
            const auto devices = VideoCaptureReader::devices();
            if (! devices.empty())
            {
                item.deviceId = devices.front().id;
                item.deviceName = devices.front().name;
                item.audioDevice = CaptureDeviceSource::guessAudioInput (item.deviceName, AudioInputSource::inputNames());
            }
        }
        scene->items.push_back (item);
        const int id = item.id;
        stage.selectItem (id);
        stage.scenesChanged();
        if (type == SourceType::screen)
            stage.fitToScreen (id);   // ya se sabe su tamano
        if (type == SourceType::captureDevice)
            stage.fitWhenReady (id);
        // Como OBS: al crearla se abren sus propiedades (las de la mezcla y los decks no tienen).
        auto& st = stage;
        const bool hasSettings = type != SourceType::decks && type != SourceType::deckA && type != SourceType::deckB;
        juce::MessageManager::callAsync ([&st, id, hasSettings]
        {
            if (hasSettings) st.showProperties (id);
            else             st.closeDialogs();
        });
    }

    void addSelectedExisting()
    {
        const int row = existing.getSelectedRow();
        auto& scenes = stage.getScenes();
        auto* scene = scenes.findScene (scenes.currentScene);
        if (scene == nullptr || ! juce::isPositiveAndBelow (row, (int) existingItems.size()))
            return;
        auto item = existingItems[(size_t) row];   // mismos ajustes: la misma captura o el mismo video
        item.id = scenes.newId();
        item.visible = makeVisible.getToggleState();
        scene->items.push_back (item);
        stage.selectItem (item.id);
        stage.scenesChanged();
        auto& st = stage;
        juce::MessageManager::callAsync ([&st] { st.closeDialogs(); });
    }

    VideoStage& stage;
    TypeModel typeModel { *this };
    ExistingModel existingModel { *this };
    juce::ListBox types, existing;
    std::vector<SceneItem> existingItems;
    juce::Label heading, hint, empty;
    CreateButton createButton;
    juce::TextButton addExisting { "Anadir existente" }, closeButton { "Cerrar" };
    juce::ToggleButton makeVisible { "Hacer visible la fuente" };
};

AddSourceWindow::AddSourceWindow (VideoStage& s)
    : juce::DocumentWindow ("Anadir fuente", DarkLookAndFeel::panel, juce::DocumentWindow::closeButton), stage (s)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (stage), true);
    setResizable (true, false);
    setResizeLimits (640, 420, 1600, 1200);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
    toFront (true);
}

void AddSourceWindow::closeButtonPressed()
{
    auto& st = stage;
    juce::MessageManager::callAsync ([&st] { st.closeDialogs(); });
}

// --- Propiedades ---------------------------------------------------------------------------------

class SourcePropertiesWindow::Content : public juce::Component,
                                         private juce::Timer
{
public:
    Content (VideoStage& s, int id) : stage (s), itemId (id)
    {
        auto* item = stage.getScenes().findItem (itemId);
        jassert (item != nullptr);
        snapshot = *item;
        type = item->type;

        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (preview);
        addAndMakeVisible (viewport);
        defaultsButton.onClick = [this]
        {
            edit ([] (SceneItem& i) { i.resetSourceSettings(); });
            reload();
        };
        okButton.onClick = [this] { close(); };
        cancelButton.onClick = [this]
        {
            if (auto* i = stage.getScenes().findItem (itemId))
            {
                *i = snapshot;   // todo como estaba al abrir
                stage.scenesChanged();
            }
            close();
        };
        for (auto* b : { &defaultsButton, &okButton, &cancelButton })
            addAndMakeVisible (b);

        build();
        reload();
        startTimerHz (5);
        setSize (740, 660);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (DarkLookAndFeel::panel);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        preview.setBounds (r.removeFromTop (hasPreview() ? 220 : 0));
        if (hasPreview())
            r.removeFromTop (10);
        auto bar = r.removeFromBottom (32);
        defaultsButton.setBounds (bar.removeFromLeft (110));
        cancelButton.setBounds (bar.removeFromRight (100));
        bar.removeFromRight (8);
        okButton.setBounds (bar.removeFromRight (100));
        r.removeFromBottom (10);
        viewport.setBounds (r);
        list.setSize (r.getWidth() - viewport.getScrollBarThickness(), list.layOut (r.getWidth() - viewport.getScrollBarThickness()));
        if (transportLayout) transportLayout();
        if (buttonsLayout) buttonsLayout();
    }

private:
    // La imagen de la fuente, arriba (como la vista de la ventana de propiedades de OBS): la
    // compone el compositor (asi se ven tambien el video que se decodifica en la tarjeta grafica
    // y las capturas). Sin Direct3D, lo que se pueda dibujar aqui.
    struct Preview : public juce::Component
    {
        explicit Preview (Content& c) : owner (c)
        {
            view = owner.stage.createSourceView (owner.itemId);
            if (view != nullptr)
            {
                view->onRightClick = [] (const juce::MouseEvent&) {};
                addAndMakeVisible (*view);
            }
        }
        void resized() override
        {
            if (view != nullptr)
                view->setBounds (getLocalBounds());
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::black);
            if (view != nullptr)
                return;
            auto r = getLocalBounds().toFloat();
            juce::Image image;
            if (auto source = owner.stage.getProgram().frameSourceFor (owner.itemId))
                image = source->getFrame();
            else if (owner.type == SourceType::image)
                if (auto* item = owner.stage.getScenes().findItem (owner.itemId))
                    image = juce::ImageCache::getFromFile (item->file);
            if (image.isValid())
            {
                g.drawImageWithin (image, 0, 0, getWidth(), getHeight(), juce::RectanglePlacement::centred);
                return;
            }
            Icons::drawSource (g, r.withSizeKeepingCentre (56.0f, 56.0f).translated (0.0f, -14.0f), owner.type, DarkLookAndFeel::textQuiet);
            g.setColour (DarkLookAndFeel::textQuiet);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ((owner.type == SourceType::media || owner.type == SourceType::captureDevice) && ! owner.stage.isRunning()
                            ? "La imagen se ve con la pestana VIDEO del mezclador abierta" : "",
                        r.withTrimmedTop (r.getHeight() * 0.5f + 24.0f), juce::Justification::centredTop, true);
        }
        Content& owner;
        std::unique_ptr<ProjectorView> view;
    };

    // Filas "etiqueta: control" con la etiqueta a la derecha, como las propiedades de OBS.
    struct PropertyList : public juce::Component
    {
        struct Row
        {
            std::unique_ptr<juce::Label> label;
            juce::Component* editor = nullptr;
            int height = 26;
            juce::Component* right = nullptr;   // p. ej. un boton Examinar a la derecha
            int rightWidth = 0;
        };

        void add (const juce::String& text, juce::Component& editor, int height = 26, juce::Component* right = nullptr, int rightWidth = 96)
        {
            Row row;
            if (text.isNotEmpty())
            {
                row.label = std::make_unique<juce::Label> (text, text);
                row.label->setJustificationType (juce::Justification::centredRight);
                row.label->setFont (juce::FontOptions (14.0f));
                addAndMakeVisible (*row.label);
            }
            row.editor = &editor;
            row.height = height;
            row.right = right;
            row.rightWidth = right != nullptr ? rightWidth : 0;
            addAndMakeVisible (editor);
            if (right != nullptr)
                addAndMakeVisible (*right);
            rows.push_back (std::move (row));
        }

        void section (const juce::String& text)
        {
            auto& l = *headings.emplace_back (std::make_unique<juce::Label> (text, text));
            l.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            l.setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
            Row row;
            row.editor = &l;
            row.height = 30;
            addAndMakeVisible (l);
            rows.push_back (std::move (row));
        }

        int layOut (int width)
        {
            constexpr int labelWidth = 200;
            int y = 4;
            for (auto& row : rows)
            {
                juce::Rectangle<int> r (0, y, width, row.height);
                if (row.editor != nullptr && row.label == nullptr && dynamic_cast<juce::Label*> (row.editor) != nullptr)
                {
                    row.editor->setBounds (r.reduced (4, 0));   // titulo de seccion
                }
                else
                {
                    auto labelArea = r.removeFromLeft (labelWidth);
                    if (row.label != nullptr)
                        row.label->setBounds (labelArea.withTrimmedRight (10));
                    if (row.right != nullptr)
                    {
                        row.right->setBounds (r.removeFromRight (row.rightWidth).reduced (0, 1));
                        r.removeFromRight (6);
                    }
                    row.editor->setBounds (r.withTrimmedRight (8).reduced (0, 1));
                }
                y += row.height + 6;
            }
            return y + 4;
        }

        void resized() override { layOut (getWidth()); }

        std::vector<Row> rows;
        std::vector<std::unique_ptr<juce::Label>> headings;
    };

    bool hasPreview() const
    {
        // Con el compositor se ven todas; sin el, solo las que tienen imagen en la memoria normal.
        return preview.view != nullptr || type == SourceType::media || type == SourceType::captureDevice || type == SourceType::image;
    }

    template <typename C, typename... Args>
    C& make (Args&&... args)
    {
        auto c = std::make_unique<C> (std::forward<Args> (args)...);
        auto& ref = *c;
        owned.push_back (std::move (c));
        return ref;
    }

    // Cambiar el elemento (si existe) y publicar el cambio.
    void edit (std::function<void (SceneItem&)> change)
    {
        if (updating)
            return;
        if (auto* i = stage.getScenes().findItem (itemId))
        {
            change (*i);
            stage.scenesChanged();
        }
    }

    // Volver a leer todos los controles del elemento (al abrir y tras "Por defecto").
    void reload()
    {
        auto* i = stage.getScenes().findItem (itemId);
        if (i == nullptr)
            return;
        const juce::ScopedValueSetter<bool> svs (updating, true);
        for (auto& load : loaders)
            load (*i);
        timerCallback();
    }

    juce::ToggleButton& toggle (const juce::String& text, bool SceneItem::* field, std::function<void (SceneItem&)> after = {})
    {
        auto& t = make<juce::ToggleButton> (text);
        t.onClick = [this, &t, field, after]
        {
            edit ([&t, field, after] (SceneItem& i) { i.*field = t.getToggleState(); if (after) after (i); });
        };
        loaders.push_back ([&t, field] (const SceneItem& i) { t.setToggleState (i.*field, juce::dontSendNotification); });
        list.add ({}, t);
        return t;
    }

    juce::Slider& slider (const juce::String& label, double min, double max, double step, const juce::String& suffix,
                          std::function<double (const SceneItem&)> get, std::function<void (SceneItem&, double)> set)
    {
        auto& s = make<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
        s.setRange (min, max, step);
        s.setTextValueSuffix (suffix);
        s.onValueChange = [this, &s, set] { edit ([&s, set] (SceneItem& i) { set (i, s.getValue()); }); };
        loaders.push_back ([&s, get] (const SceneItem& i) { s.setValue (get (i), juce::dontSendNotification); });
        list.add (label, s);
        return s;
    }

    juce::TextButton& colourButton()
    {
        auto& b = make<juce::TextButton> ("Elegir color...");
        b.onClick = [this, &b]
        {
            auto* item = stage.getScenes().findItem (itemId);
            if (item == nullptr)
                return;
            const int id = itemId;
            auto& st = stage;
            auto picker = std::make_unique<ColourPicker> (item->colour, [&st, id] (juce::Colour c)
            {
                if (auto* i = st.getScenes().findItem (id))
                {
                    i->colour = c;
                    st.scenesChanged();
                }
            });
            juce::CallOutBox::launchAsynchronously (std::move (picker), b.getScreenBounds(), nullptr);
        };
        list.add ("Color", b);
        return b;
    }

    // Archivo con su boton Examinar (imagen o multimedia).
    void fileRow (const juce::String& label, const juce::String& filter, std::function<void (SceneItem&)> afterChoose)
    {
        auto& path = make<juce::TextEditor>();
        auto& browse = make<juce::TextButton> ("Examinar");
        path.onReturnKey = [this, &path, afterChoose]
        {
            edit ([&path, afterChoose] (SceneItem& i) { i.file = juce::File (path.getText().trim().unquoted()); if (afterChoose) afterChoose (i); });
        };
        path.onFocusLost = path.onReturnKey;
        browse.onClick = [this, &path, filter, afterChoose, label]
        {
            auto* item = stage.getScenes().findItem (itemId);
            const auto start = item != nullptr && item->file.existsAsFile() ? item->file
                             : juce::File::getSpecialLocation (type == SourceType::image ? juce::File::userPicturesDirectory
                                                                                         : juce::File::userMoviesDirectory);
            chooser = std::make_unique<juce::FileChooser> (label, start, filter);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, &path, afterChoose] (const juce::FileChooser& fc)
            {
                if (fc.getResult() == juce::File())
                    return;
                path.setText (fc.getResult().getFullPathName(), false);
                edit ([&fc, afterChoose] (SceneItem& i) { i.file = fc.getResult(); if (afterChoose) afterChoose (i); });
            });
        };
        loaders.push_back ([&path] (const SceneItem& i) { path.setText (i.file.getFullPathName(), false); });
        list.add (label, path, 26, &browse);
    }

    void build()
    {
        // Nombre (todas)
        auto& name = make<juce::TextEditor>();
        name.onTextChange = [this, &name] { edit ([&name] (SceneItem& i) { i.name = name.getText().trim().isEmpty() ? i.name : name.getText(); }); };
        loaders.push_back ([&name] (const SceneItem& i) { if (name.getText() != i.name) name.setText (i.name, false); });
        list.add ("Nombre", name);

        switch (type)
        {
            case SourceType::media:         buildMedia(); break;
            case SourceType::captureDevice: buildCaptureDevice(); break;
            case SourceType::screen:
            case SourceType::window:        buildCapture(); break;
            case SourceType::image:
                fileRow ("Archivo de imagen", "*.png;*.jpg;*.jpeg;*.gif;*.bmp", [this] (SceneItem& i) { centreImageLater (i.id); });
                break;
            case SourceType::text:          buildText(); break;
            case SourceType::colour:
                colourButton();
                slider ("Ancho", 1.0, 7680.0, 1.0, " px", [] (const SceneItem& i) { return (double) i.width; }, [] (SceneItem& i, double v) { i.width = (int) v; });
                slider ("Alto", 1.0, 4320.0, 1.0, " px", [] (const SceneItem& i) { return (double) i.height; }, [] (SceneItem& i, double v) { i.height = (int) v; });
                break;
            case SourceType::visualizer:    buildVisualizer(); break;
            case SourceType::decks:
            case SourceType::deckA:
            case SourceType::deckB:
            default:
                break;
        }
        buildTransform();
    }

    void buildMedia()
    {
        fileRow ("Archivo local", MediaFileSource::fileFilter(), [this] (SceneItem& i) { stage.fitWhenReady (i.id); });
        toggle ("Bucle", &SceneItem::loop);
        toggle ("Reiniciar la reproduccion cuando la fuente este activa", &SceneItem::restartOnActivate);
        toggle ("Utilizar la decodificacion por hardware cuando este disponible", &SceneItem::hardwareDecode);
        toggle ("No mostrar nada al terminar la reproduccion", &SceneItem::clearOnEnd);
        toggle ("Cerrar archivo cuando este inactivo", &SceneItem::closeWhenInactive);
        // Al cambiarlo cambia el tamano de la fuente (la mitad de ancho): se vuelve a encajar.
        toggle ("Video con mate (color a la izquierda, transparencia a la derecha)", &SceneItem::matte,
                [this] (SceneItem& i) { stage.fitWhenReady (i.id); })
            .setTooltip ("Para videos con transparencia: el color en la mitad izquierda y en la derecha la transparencia en grises "
                         "(blanco = se ve). Como convertir un video con transparencia: LEEME, 'Videos con mate'");
        slider ("Velocidad", 1.0, 200.0, 1.0, " %", [] (const SceneItem& i) { return (double) i.speedPercent; },
                [] (SceneItem& i, double v) { i.speedPercent = (int) v; });
        buildVolume();

        // Controles de reproduccion, como los de OBS (abajo de la vista previa).
        auto& transport = make<juce::Component>();
        playButton = &make<IconButton> (Icons::Glyph::play, "Reproducir / pausar");
        auto& stopButton = make<IconButton> (Icons::Glyph::stop, "Detener");
        auto& restartButton = make<IconButton> (Icons::Glyph::restart, "Reiniciar");
        timeLabel = &make<juce::Label>();
        timeLabel->setColour (juce::Label::textColourId, DarkLookAndFeel::textQuiet);
        for (auto* c : std::initializer_list<juce::Component*> { playButton, &stopButton, &restartButton, timeLabel })
            transport.addAndMakeVisible (c);
        playButton->onClick = [this] { if (auto m = mediaSource()) { m->isPlaying() ? m->pause() : m->play(); } };
        stopButton.onClick = [this] { if (auto m = mediaSource()) m->stop(); };
        restartButton.onClick = [this] { if (auto m = mediaSource()) m->restart(); };
        transportLayout = [&transport, &stopButton, &restartButton, this]
        {
            auto r = transport.getLocalBounds();
            playButton->setBounds (r.removeFromLeft (30));
            stopButton.setBounds (r.removeFromLeft (30));
            restartButton.setBounds (r.removeFromLeft (30));
            r.removeFromLeft (8);
            timeLabel->setBounds (r);
        };
        list.add ("Controles", transport, 30);
        statusRow();
    }

    void buildVolume()
    {
        slider ("Volumen", 0.0, 100.0, 1.0, " %", [] (const SceneItem& i) { return i.volume * 100.0; },
                [] (SceneItem& i, double v) { i.volume = (float) (v / 100.0); });
        toggle ("Silenciar", &SceneItem::muted);
    }

    void statusRow()
    {
        statusLabel = &make<juce::Label>();
        statusLabel->setColour (juce::Label::textColourId, juce::Colour (0xffffb74d));
        statusLabel->setFont (juce::FontOptions (13.0f));
        list.add ({}, *statusLabel, 22);
    }

    void buildCaptureDevice()
    {
        // Dispositivo
        auto& device = make<juce::ComboBox>();
        auto& activate = make<juce::TextButton> ("Desactivar");
        devices = VideoCaptureReader::devices();
        loaders.push_back ([this, &device] (const SceneItem& i)
        {
            device.clear (juce::dontSendNotification);
            int chosen = 0;
            for (size_t d = 0; d < devices.size(); ++d)
            {
                device.addItem (devices[d].name, (int) d + 1);
                if (devices[d].id == i.deviceId)
                    chosen = (int) d + 1;
            }
            if (chosen == 0 && i.deviceId.isNotEmpty())
            {
                device.addItem (i.deviceName + " (no conectado)", 1000);
                chosen = 1000;
            }
            device.setSelectedId (chosen, juce::dontSendNotification);
        });
        device.setTextWhenNoChoicesAvailable ("No hay dispositivos de captura de video");
        device.onChange = [this, &device]
        {
            const int d = device.getSelectedId() - 1;
            if (! juce::isPositiveAndBelow (d, (int) devices.size()))
                return;
            const auto chosen = devices[(size_t) d];
            edit ([chosen] (SceneItem& i)
            {
                const bool first = i.deviceId.isEmpty();
                i.deviceId = chosen.id;
                i.deviceName = chosen.name;
                i.customFormat = false;
                if (first || i.audioDevice.isEmpty())
                    i.audioDevice = CaptureDeviceSource::guessAudioInput (chosen.name, AudioInputSource::inputNames());
            });
            stage.fitWhenReady (itemId);
            reload();
        };
        list.add ("Dispositivo", device);
        activate.onClick = [this] { edit ([] (SceneItem& i) { i.deviceActive = ! i.deviceActive; }); reload(); };
        loaders.push_back ([&activate] (const SceneItem& i) { activate.setButtonText (i.deviceActive ? "Desactivar" : "Activar"); });
        list.add ({}, activate);
        toggle ("Desactivar cuando no se muestre", &SceneItem::deactivateWhenHidden);

        // Resolucion / FPS / formato
        list.section ("VIDEO");
        auto& resType = make<juce::ComboBox>();
        resType.addItem ("Dispositivo predeterminado", 1);
        resType.addItem ("Personalizado", 2);
        auto& resolution = make<juce::ComboBox>();
        auto& fps = make<juce::ComboBox>();
        auto& format = make<juce::ComboBox>();
        loaders.push_back ([&resType, &resolution, &fps, &format] (const SceneItem& i)
        {
            resType.setSelectedId (i.customFormat ? 2 : 1, juce::dontSendNotification);
            const auto& fmts = formatsOf (i.deviceId);
            resolution.clear (juce::dontSendNotification);
            fps.clear (juce::dontSendNotification);
            format.clear (juce::dontSendNotification);
            juce::StringArray sizes;
            for (const auto& f : fmts)
                sizes.addIfNotAlreadyThere (juce::String (f.width) + "x" + juce::String (f.height));
            for (int k = 0; k < sizes.size(); ++k)
                resolution.addItem (sizes[k], k + 1);
            // Sin elegir: el tamano mas grande (como el primero de la lista de OBS).
            const auto current = i.captureWidth > 0 ? juce::String (i.captureWidth) + "x" + juce::String (i.captureHeight) : sizes[0];
            resolution.setSelectedId (sizes.indexOf (current) + 1, juce::dontSendNotification);
            fps.addItem ("El mas alto", 1);
            format.addItem ("Cualquiera", 1);
            juce::Array<double> rates;
            juce::StringArray kinds;
            for (const auto& f : fmts)
                if (juce::String (f.width) + "x" + juce::String (f.height) == current)
                {
                    rates.addIfNotAlreadyThere (f.fps);
                    kinds.addIfNotAlreadyThere (f.format);
                }
            for (int k = 0; k < rates.size(); ++k)
                fps.addItem (fpsText (rates[k]), k + 2);
            for (int k = 0; k < kinds.size(); ++k)
                format.addItem (kinds[k], k + 2);
            fps.setSelectedId (i.captureFps > 0.0 && rates.contains (i.captureFps) ? rates.indexOf (i.captureFps) + 2 : 1, juce::dontSendNotification);
            format.setSelectedId (kinds.contains (i.captureFormat) ? kinds.indexOf (i.captureFormat) + 2 : 1, juce::dontSendNotification);
            for (auto* c : { &resolution, &fps, &format })
                c->setEnabled (i.customFormat);
        });
        auto setFormat = [this, &resolution, &fps, &format]
        {
            const auto size = resolution.getText();
            const double rate = fps.getSelectedId() > 1 ? fps.getText().getDoubleValue() : 0.0;
            const auto kind = format.getSelectedId() > 1 ? format.getText() : juce::String();
            edit ([size, rate, kind] (SceneItem& i)
            {
                i.captureWidth = size.upToFirstOccurrenceOf ("x", false, false).getIntValue();
                i.captureHeight = size.fromFirstOccurrenceOf ("x", false, false).getIntValue();
                i.captureFps = rate;
                i.captureFormat = kind;
            });
            stage.fitWhenReady (itemId);
            reload();   // los fps y formatos de la resolucion nueva
        };
        resType.onChange = [this, &resType, setFormat]
        {
            edit ([&resType] (SceneItem& i) { i.customFormat = resType.getSelectedId() == 2; });
            if (resType.getSelectedId() == 2) setFormat();
            else reload();
            stage.fitWhenReady (itemId);
        };
        resolution.onChange = setFormat;
        fps.onChange = setFormat;
        format.onChange = setFormat;
        list.add ("Tipo de resolucion/FPS", resType);
        list.add ("Resolucion", resolution);
        list.add ("FPS", fps);
        list.add ("Formato de video", format);
        toggle ("Voltear verticalmente", &SceneItem::flipVertical);
        toggle ("Utilizar la decodificacion por hardware cuando este disponible", &SceneItem::hardwareDecode);

        // Audio (en las capturadoras USB/HDMI es otra entrada de Windows)
        list.section ("AUDIO");
        auto& audio = make<juce::ComboBox>();
        inputs = AudioInputSource::inputNames();
        loaders.push_back ([this, &audio] (const SceneItem& i)
        {
            audio.clear (juce::dontSendNotification);
            audio.addItem ("Sin audio", 1);
            for (int k = 0; k < inputs.size(); ++k)
                audio.addItem (inputs[k], k + 2);
            if (i.audioDevice.isNotEmpty() && ! inputs.contains (i.audioDevice))
                audio.addItem (i.audioDevice + " (no conectada)", 1000);
            audio.setSelectedId (i.audioDevice.isEmpty() ? 1 : inputs.contains (i.audioDevice) ? inputs.indexOf (i.audioDevice) + 2 : 1000,
                                 juce::dontSendNotification);
        });
        audio.onChange = [this, &audio]
        {
            const int id = audio.getSelectedId();
            if (id == 1000)
                return;
            const auto name = id >= 2 ? inputs[id - 2] : juce::String();
            edit ([name] (SceneItem& i) { i.audioDevice = name; });
        };
        audio.setTooltip ("El sonido de la capturadora va al master (en las USB/HDMI suele llamarse 'capture' o 'Digital Audio Interface')");
        list.add ("Dispositivo de audio", audio);
        buildVolume();
        statusRow();
    }

    void buildCapture()
    {
        auto& target = make<juce::ComboBox>();
        auto& refresh = make<juce::TextButton> ("Actualizar");
        target.setTextWhenNothingSelected ("Elige...");
        loaders.push_back ([this, &target] (const SceneItem& i) { fillTargets (target, i); });
        target.onChange = [this, &target]
        {
            const int index = target.getSelectedId() - 1;
            bool first = false;
            if (type == SourceType::screen && juce::isPositiveAndBelow (index, (int) monitors.size()))
            {
                const auto m = monitors[(size_t) index];
                edit ([index, m] (SceneItem& i) { i.monitor = index; i.monitorId = m.id; });
            }
            else if (type == SourceType::window && juce::isPositiveAndBelow (index, (int) windows.size()))
            {
                const auto w = windows[(size_t) index];
                if (auto* item = stage.getScenes().findItem (itemId))
                    first = item->windowTitle.isEmpty();
                edit ([w] (SceneItem& i) { i.windowTitle = w.title; i.windowClass = w.className; i.windowExe = w.exe; });
            }
            if (first)
                stage.fitToScreen (itemId);   // la primera ventana elegida se encaja en el programa
        };
        refresh.onClick = [this, &target] { if (auto* i = stage.getScenes().findItem (itemId)) fillTargets (target, *i); };
        if (type == SourceType::window)
            list.add ("Ventana", target, 26, &refresh);
        else
            list.add ("Pantalla", target);
        toggle ("Capturar el puntero", &SceneItem::cursor);
        if (type == SourceType::window)
            toggle ("Solo el contenido (sin bordes ni barra de titulo)", &SceneItem::clientArea);
        statusRow();
    }

    // La lista de pantallas o de ventanas (id = posicion + 1; la ventana guardada que ya no esta
    // abierta sale con id = 10000).
    void fillTargets (juce::ComboBox& target, const SceneItem& item)
    {
        target.clear (juce::dontSendNotification);
        if (type == SourceType::screen)
        {
            monitors = ScreenCapture::monitors();
            for (size_t m = 0; m < monitors.size(); ++m)
                target.addItem (ScreenCapture::describe (monitors[m], (int) m), (int) m + 1);
            target.setSelectedId (ScreenCapture::findMonitor (monitors, item.monitorId, item.monitor) + 1, juce::dontSendNotification);
            return;
        }
        windows = ScreenCapture::windows();
        int chosen = 0;
        for (size_t w = 0; w < windows.size(); ++w)
        {
            const auto& win = windows[w];
            target.addItem (ScreenCapture::describe (win), (int) w + 1);
            if (chosen == 0 && win.title == item.windowTitle && win.className == item.windowClass && win.exe.equalsIgnoreCase (item.windowExe))
                chosen = (int) w + 1;
        }
        if (chosen == 0 && item.windowTitle.isNotEmpty())
        {
            ScreenCapture::Window saved { nullptr, item.windowTitle, item.windowClass, item.windowExe };
            target.addItem (ScreenCapture::describe (saved), 10000);
            chosen = 10000;
        }
        target.setSelectedId (chosen, juce::dontSendNotification);
    }

    void buildText()
    {
        auto& text = make<juce::TextEditor>();
        text.setMultiLine (true);
        text.setReturnKeyStartsNewLine (true);
        text.setTooltip ("{titulo}, {artista} y {bpm} se cambian por la cancion que suena");
        text.onTextChange = [this, &text] { edit ([&text] (SceneItem& i) { i.text = text.getText(); }); };
        loaders.push_back ([&text] (const SceneItem& i) { if (text.getText() != i.text) text.setText (i.text, false); });
        list.add ("Texto", text, 64);
        slider ("Tamano", 8.0, 300.0, 1.0, {}, [] (const SceneItem& i) { return (double) i.fontSize; }, [] (SceneItem& i, double v) { i.fontSize = (float) v; });
        toggle ("Negrita", &SceneItem::bold);
        toggle ("Sombra", &SceneItem::shadow);
        colourButton();
    }

    void buildVisualizer()
    {
        auto& style = make<juce::ComboBox>();
        for (int k = 0; k < 3; ++k)
            style.addItem (SceneCollection::visualizerName ((VisualizerStyle) k), k + 1);
        style.setTooltip ("Espectro: barras de graves a agudos; Onda: osciloscopio; Pulso: late con el ritmo");
        loaders.push_back ([&style] (const SceneItem& i) { style.setSelectedId ((int) i.visualizer + 1, juce::dontSendNotification); });
        style.onChange = [this, &style]
        {
            const int w = stage.getProgramWidth(), h = stage.getProgramHeight();
            edit ([&style, w, h] (SceneItem& i)
            {
                i.visualizer = (VisualizerStyle) (style.getSelectedId() - 1);
                // Cada estilo con su sitio de siempre: el pulso, un cuadrado en el centro; los otros, abajo a lo ancho.
                if (i.visualizer == VisualizerStyle::pulse)
                {
                    i.width = i.height = h / 2;
                    i.position = { (float) (w - i.width) * 0.5f, (float) (h - i.height) * 0.5f };
                }
                else
                {
                    i.width = w;
                    i.height = h / 3;
                    i.position = { 0.0f, (float) (h - i.height) };
                }
                i.scaleX = i.scaleY = 1.0f;
                i.rotation = 0.0f;
                i.cropLeft = i.cropTop = i.cropRight = i.cropBottom = 0.0f;
            });
            reload();
        };
        list.add ("Estilo", style);
        colourButton();
    }

    void buildTransform()
    {
        list.section ("COLOCACION");
        slider ("Opacidad", 0.0, 100.0, 1.0, " %", [] (const SceneItem& i) { return i.opacity * 100.0; },
                [] (SceneItem& i, double v) { i.opacity = (float) (v / 100.0); });
        slider ("Giro", -180.0, 180.0, 1.0, juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), [] (const SceneItem& i) { return (double) i.rotation; },
                [] (SceneItem& i, double v) { i.rotation = (float) v; });
        const char* names[] = { "Recorte izquierda", "Recorte arriba", "Recorte derecha", "Recorte abajo" };
        float SceneItem::* fields[] = { &SceneItem::cropLeft, &SceneItem::cropTop, &SceneItem::cropRight, &SceneItem::cropBottom };
        for (int k = 0; k < 4; ++k)
        {
            auto field = fields[k];
            auto& s = slider (names[k], 0.0, 1.0, 1.0, " px", [field] (const SceneItem& i) { return (double) (i.*field); },
                              [field] (SceneItem& i, double v) { i.*field = (float) v; });
            // El recorte llega hasta casi todo el tamano de la fuente.
            loaders.insert (loaders.end() - 1, [this, &s, k] (const SceneItem& i)
            {
                const auto natural = stage.getProgram().naturalSize (i);
                s.setRange (0.0, juce::jmax (1.0, (k % 2 == 0 ? natural.x : natural.y) * 0.95), 1.0);
            });
        }
        auto& buttons = make<juce::Component>();
        auto& fit = make<juce::TextButton> ("Ajustar a la pantalla");
        auto& reset = make<juce::TextButton> ("Restablecer");
        buttons.addAndMakeVisible (fit);
        buttons.addAndMakeVisible (reset);
        fit.onClick = [this] { stage.fitToScreen (itemId); reload(); };
        reset.onClick = [this] { stage.resetTransform (itemId); reload(); };
        buttonsLayout = [&buttons, &fit, &reset]
        {
            auto r = buttons.getLocalBounds();
            fit.setBounds (r.removeFromLeft (r.getWidth() / 2).withTrimmedRight (3));
            reset.setBounds (r.withTrimmedLeft (3));
        };
        list.add ({}, buttons, 28);
    }

    // Una imagen recien elegida se centra a su tamano (si no cabe, encajada).
    void centreImageLater (int id)
    {
        auto& st = stage;
        juce::MessageManager::callAsync ([&st, id]
        {
            auto* item = st.getScenes().findItem (id);
            if (item == nullptr)
                return;
            const auto natural = st.getProgram().naturalSize (*item);
            const float w = (float) st.getProgramWidth(), h = (float) st.getProgramHeight();
            const float scale = juce::jmin (1.0f, w / (float) juce::jmax (1, natural.x), h / (float) juce::jmax (1, natural.y));
            item->scaleX = item->scaleY = scale;
            item->position = { (w - (float) natural.x * scale) * 0.5f, (h - (float) natural.y * scale) * 0.5f };
            st.scenesChanged();
        });
    }

    std::shared_ptr<MediaFileSource> mediaSource() const
    {
        return std::dynamic_pointer_cast<MediaFileSource> (stage.getProgram().frameSourceFor (itemId));
    }

    void timerCallback() override
    {
        if (transportLayout) transportLayout();
        if (buttonsLayout) buttonsLayout();
        if (hasPreview() && preview.view == nullptr)
            preview.repaint();
        if (statusLabel == nullptr)
            return;
        juce::String text;
        if (type == SourceType::screen || type == SourceType::window)
        {
            if (auto capture = stage.getProgram().captureFor (itemId))
            {
                text = capture->getStatus();
                if (text.isEmpty())
                {
                    const auto size = capture->getSize();
                    text = size.x > 0 ? "Capturando " + juce::String (size.x) + "x" + juce::String (size.y) + " (" + capture->getMethod() + ")"
                                      : "Esperando imagen...";
                }
            }
        }
        else if (auto source = stage.getProgram().frameSourceFor (itemId))
        {
            text = source->getStatus();
            if (text.isEmpty() && source->getSize().x > 0)
                text = "Imagen de " + juce::String (source->getSize().x) + "x" + juce::String (source->getSize().y);
            if (auto m = std::dynamic_pointer_cast<MediaFileSource> (source))
            {
                playButton->setGlyph (m->isPlaying() ? Icons::Glyph::pause : Icons::Glyph::play);
                timeLabel->setText (formatTime (m->getPosition()) + " / " + formatTime (m->getDuration()), juce::dontSendNotification);
            }
        }
        const bool ok = text.isEmpty() || text.startsWith ("Capturando") || text.startsWith ("Imagen");
        statusLabel->setColour (juce::Label::textColourId, ok ? DarkLookAndFeel::textQuiet : juce::Colour (0xffffb74d));
        if (statusLabel->getText() != text)
            statusLabel->setText (text, juce::dontSendNotification);
    }

    void close()
    {
        auto& st = stage;
        juce::MessageManager::callAsync ([&st] { st.closeDialogs(); });
    }

    VideoStage& stage;
    const int itemId;
    SourceType type = SourceType::decks;
    SceneItem snapshot;
    bool updating = false;

    Preview preview { *this };
    juce::Viewport viewport;
    PropertyList list;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::function<void (const SceneItem&)>> loaders;
    std::function<void()> transportLayout, buttonsLayout;
    juce::TextButton defaultsButton { "Por defecto" }, okButton { "Aceptar" }, cancelButton { "Cancelar" };
    juce::Label* statusLabel = nullptr;
    juce::Label* timeLabel = nullptr;
    IconButton* playButton = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<VideoCaptureReader::Device> devices;
    juce::StringArray inputs;
    std::vector<ScreenCapture::Monitor> monitors;
    std::vector<ScreenCapture::Window> windows;

    friend struct Preview;
};

SourcePropertiesWindow::SourcePropertiesWindow (VideoStage& s, int id)
    : juce::DocumentWindow ("Propiedades", DarkLookAndFeel::panel, juce::DocumentWindow::closeButton), stage (s), itemId (id)
{
    if (auto* item = stage.getScenes().findItem (id))
        setName ("Propiedades para '" + item->name + "'");
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (stage, id), true);
    setResizable (true, false);
    setResizeLimits (560, 420, 1600, 1400);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
    toFront (true);
}

void SourcePropertiesWindow::closeButtonPressed()
{
    auto& st = stage;
    juce::MessageManager::callAsync ([&st] { st.closeDialogs(); });
}
}
