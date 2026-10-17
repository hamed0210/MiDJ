#include "UI/ScenePanel.h"

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

    const juce::Colour accent (0xff29b6f6);
}

// --- Lista de escenas ------------------------------------------------------------------------

class ScenePanel::SceneListModel : public juce::ListBoxModel
{
public:
    explicit SceneListModel (ScenePanel& p) : panel (p) {}
    int getNumRows() override { return (int) panel.scenes.scenes.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (! juce::isPositiveAndBelow (row, getNumRows()))
            return;
        const auto& scene = panel.scenes.scenes[(size_t) row];
        const bool live = scene.id == panel.scenes.currentScene;
        if (live)
            g.fillAll (accent.withAlpha (0.35f));
        g.setColour (live ? juce::Colours::white : juce::Colours::lightgrey);
        g.setFont (juce::FontOptions (14.0f, live ? juce::Font::bold : juce::Font::plain));
        g.drawText (scene.name, 8, 0, w - 16, h, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        if (juce::isPositiveAndBelow (row, getNumRows()) && panel.onSceneChosen)
            panel.onSceneChosen (panel.scenes.scenes[(size_t) row].id);
    }

private:
    ScenePanel& panel;
};

// --- Lista de elementos (arriba el que se ve encima, como en OBS) ------------------------------

class ScenePanel::ItemListModel : public juce::ListBoxModel
{
public:
    explicit ItemListModel (ScenePanel& p) : panel (p) {}

    SceneItem* itemAt (int row)
    {
        auto* scene = panel.currentScene();
        if (scene == nullptr || ! juce::isPositiveAndBelow (row, (int) scene->items.size()))
            return nullptr;
        return &scene->items[scene->items.size() - 1 - (size_t) row];
    }

    int getNumRows() override
    {
        auto* scene = panel.currentScene();
        return scene != nullptr ? (int) scene->items.size() : 0;
    }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool isSelected) override
    {
        auto* item = itemAt (row);
        if (item == nullptr)
            return;
        if (isSelected)
            g.fillAll (juce::Colour (0xffff3030).withAlpha (0.3f));
        // Ojo: visible o no (clic en el para cambiarlo).
        g.setColour (item->visible ? accent : juce::Colours::grey);
        const juce::Rectangle<float> eye (6.0f, (float) h * 0.5f - 5.0f, 10.0f, 10.0f);
        if (item->visible) g.fillEllipse (eye);
        else               g.drawEllipse (eye, 1.5f);
        g.setColour (item->visible ? juce::Colours::white : juce::Colours::grey);
        g.setFont (juce::FontOptions (14.0f));
        auto label = item->name;
        if (item->type == SourceType::text)
            label << ": " << item->text.upToFirstOccurrenceOf ("\n", false, false);
        else if (item->type == SourceType::image && item->file != juce::File())
            label << ": " << item->file.getFileName();
        else if (item->type == SourceType::window && item->windowTitle.isNotEmpty())
            label << ": " << item->windowTitle;
        else if (item->type == SourceType::screen)
            label << ": " << juce::String (item->monitor + 1);
        g.drawText (label, 24, 0, w - 28, h, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        auto* item = itemAt (row);
        if (item == nullptr)
            return;
        if (e.x < 22)
        {
            item->visible = ! item->visible;
            panel.changed();
        }
        panel.selectedItem = item->id;
        panel.updateProperties();
        if (panel.onItemSelected)
            panel.onItemSelected (item->id);
    }

private:
    ScenePanel& panel;
};

// --- Panel -------------------------------------------------------------------------------------

ScenePanel::ScenePanel (SceneCollection& s, const VideoProgram& p, int w, int h)
    : scenes (s), program (p), width (w), height (h)
{
    sceneModel = std::make_unique<SceneListModel> (*this);
    itemModel = std::make_unique<ItemListModel> (*this);

    for (auto* l : { &scenesLabel, &itemsLabel, &propsLabel })
    {
        l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, juce::Colours::grey);
        addAndMakeVisible (*l);
    }
    for (auto* list : { &sceneList, &itemList })
    {
        list->setRowHeight (24);
        list->setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff111318));
        addAndMakeVisible (*list);
    }
    sceneList.setModel (sceneModel.get());
    itemList.setModel (itemModel.get());

    addScene.setTooltip ("Nueva escena");
    addScene.onClick = [this]
    {
        auto& scene = scenes.addScene ("Escena " + juce::String (scenes.scenes.size()));
        scene.items.push_back (scenes.makeItem (SourceType::decks, width, height));
        changed();
        if (onSceneChosen)
            onSceneChosen (scene.id);
    };
    removeScene.setTooltip ("Borrar la escena al aire");
    removeScene.onClick = [this]
    {
        if (scenes.scenes.size() <= 1)
            return;   // siempre queda una
        const int id = scenes.currentScene;
        std::erase_if (scenes.scenes, [id] (const Scene& sc) { return sc.id == id; });
        selectedItem = 0;
        changed();
        if (onSceneChosen)
            onSceneChosen (scenes.scenes.front().id);
    };
    renameScene.onClick = [this]
    {
        auto* scene = currentScene();
        if (scene == nullptr)
            return;
        auto* window = new juce::AlertWindow ("Renombrar escena", {}, juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", scene->name);
        window->addButton ("Aceptar", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        const int id = scene->id;
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window, id] (int result)
        {
            const auto name = window->getTextEditorContents ("name").trim();
            if (auto* sc = scenes.findScene (id); sc != nullptr && result == 1 && name.isNotEmpty())
            {
                sc->name = name;
                changed();
            }
        }), true);
    };
    for (auto* b : { &addScene, &removeScene, &renameScene, &addItemButton, &removeItem, &moveUp, &moveDown })
        addAndMakeVisible (*b);

    for (int t = 0; t <= (int) TransitionType::swipe; ++t)
        transitionType.addItem (SceneCollection::transitionName ((TransitionType) t), t + 1);
    transitionType.setTooltip ("Transicion al cambiar de escena");
    transitionType.onChange = [this]
    {
        if (updating) return;
        scenes.transition.type = (TransitionType) (transitionType.getSelectedId() - 1);
        changed();
    };
    addAndMakeVisible (transitionType);
    transitionMs.setSliderStyle (juce::Slider::LinearHorizontal);
    transitionMs.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    transitionMs.setRange (0.0, 3000.0, 50.0);
    transitionMs.setTextValueSuffix (" ms");
    transitionMs.setTooltip ("Duracion de la transicion");
    transitionMs.onValueChange = [this]
    {
        if (updating) return;
        scenes.transition.milliseconds = (int) transitionMs.getValue();
        changed();
    };
    addAndMakeVisible (transitionMs);

    addItemButton.setTooltip ("Anadir una fuente a la escena");
    addItemButton.onClick = [this]
    {
        juce::PopupMenu menu;
        for (auto type : { SourceType::decks, SourceType::deckA, SourceType::deckB, SourceType::image, SourceType::text, SourceType::colour,
                           SourceType::screen, SourceType::window })
            menu.addItem (SceneCollection::typeName (type), [this, type] { addItem (type); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addItemButton));
    };
    removeItem.setTooltip ("Quitar la fuente elegida (Supr)");
    removeItem.onClick = [this] { deleteSelectedItem(); };
    moveUp.onClick = [this] { moveSelected (+1); };
    moveDown.onClick = [this] { moveSelected (-1); };

    // Propiedades
    textEditor.setMultiLine (true);
    textEditor.setReturnKeyStartsNewLine (true);
    textEditor.setTooltip ("Texto; {titulo}, {artista} y {bpm} se cambian por la cancion que suena");
    textEditor.onTextChange = [this]
    {
        if (updating) return;
        if (auto* i = selected())
        {
            i->text = textEditor.getText();
            changed();
        }
    };
    addChildComponent (textEditor);
    addSlider (fontSize, 8.0, 300.0, 1.0, [] (SceneItem& i, float v) { i.fontSize = v; });
    addSlider (opacity, 0.0, 100.0, 1.0, [] (SceneItem& i, float v) { i.opacity = v / 100.0f; }).setTextValueSuffix (" %");
    addSlider (rotation, -180.0, 180.0, 1.0, [] (SceneItem& i, float v) { i.rotation = v; });
    addSlider (cropLeft, 0.0, 1.0, 1.0, [] (SceneItem& i, float v) { i.cropLeft = v; });
    addSlider (cropTop, 0.0, 1.0, 1.0, [] (SceneItem& i, float v) { i.cropTop = v; });
    addSlider (cropRight, 0.0, 1.0, 1.0, [] (SceneItem& i, float v) { i.cropRight = v; });
    addSlider (cropBottom, 0.0, 1.0, 1.0, [] (SceneItem& i, float v) { i.cropBottom = v; });
    for (auto* slider : { &cropLeft, &cropTop, &cropRight, &cropBottom })
        slider->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    for (auto* l : { &fontSizeLabel, &opacityLabel, &rotationLabel, &cropLabel })
    {
        l->setFont (juce::FontOptions (13.0f));
        addChildComponent (*l);
    }
    boldToggle.onClick = [this]
    {
        if (auto* i = selected(); i != nullptr && ! updating) { i->bold = boldToggle.getToggleState(); changed(); }
    };
    shadowToggle.onClick = [this]
    {
        if (auto* i = selected(); i != nullptr && ! updating) { i->shadow = shadowToggle.getToggleState(); changed(); }
    };
    colourButton.onClick = [this] { chooseColour(); };
    fileButton.onClick = [this] { chooseImage(); };
    fitButton.onClick = [this] { fitSelectedToScreen(); };
    resetButton.onClick = [this] { resetSelected(); };
    for (auto* c : std::initializer_list<juce::Component*> { &boldToggle, &shadowToggle, &colourButton, &fileButton, &fitButton, &resetButton })
        addChildComponent (*c);

    captureTarget.setTextWhenNothingSelected ("Elige...");
    captureTarget.onChange = [this] { if (! updating) chooseCaptureTarget(); };
    refreshTargets.setTooltip ("Volver a leer las ventanas abiertas");
    refreshTargets.onClick = [this] { fillCaptureTargets(); };
    cursorToggle.onClick = [this]
    {
        if (auto* i = selected(); i != nullptr && ! updating) { i->cursor = cursorToggle.getToggleState(); changed(); }
    };
    clientToggle.onClick = [this]
    {
        if (auto* i = selected(); i != nullptr && ! updating) { i->clientArea = clientToggle.getToggleState(); changed(); }
    };
    captureStatus.setFont (juce::FontOptions (12.0f));
    captureStatus.setColour (juce::Label::textColourId, juce::Colour (0xffffb74d));
    for (auto* c : std::initializer_list<juce::Component*> { &captureTarget, &refreshTargets, &cursorToggle, &clientToggle, &captureStatus })
        addChildComponent (*c);
    startTimer (500);

    refresh();
}

ScenePanel::~ScenePanel()
{
    sceneList.setModel (nullptr);
    itemList.setModel (nullptr);
}

juce::Slider& ScenePanel::addSlider (juce::Slider& s, double min, double max, double step, std::function<void (SceneItem&, float)> apply)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    s.setRange (min, max, step);
    s.onValueChange = [this, &s, apply]
    {
        if (updating) return;
        if (auto* i = selected())
        {
            apply (*i, (float) s.getValue());
            changed();
        }
    };
    addChildComponent (s);
    return s;
}

Scene* ScenePanel::currentScene() { return scenes.findScene (scenes.currentScene); }

SceneItem* ScenePanel::selected()
{
    if (auto* scene = currentScene())
        for (auto& i : scene->items)
            if (i.id == selectedItem)
                return &i;
    return nullptr;
}

void ScenePanel::changed()
{
    if (onChanged)
        onChanged();
    sceneList.updateContent();
    itemList.updateContent();
    sceneList.repaint();
    itemList.repaint();
}

void ScenePanel::refresh()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    if (selected() == nullptr)
        selectedItem = 0;
    sceneList.updateContent();
    itemList.updateContent();
    sceneList.repaint();
    itemList.repaint();
    transitionType.setSelectedId ((int) scenes.transition.type + 1, juce::dontSendNotification);
    transitionMs.setValue (scenes.transition.milliseconds, juce::dontSendNotification);
    updating = false;
    setSelectedItem (selectedItem);
}

void ScenePanel::setSelectedItem (int id)
{
    selectedItem = id;
    int row = -1;
    if (auto* scene = currentScene())
        for (size_t i = 0; i < scene->items.size(); ++i)
            if (scene->items[i].id == id)
                row = (int) (scene->items.size() - 1 - i);
    if (row >= 0) itemList.selectRow (row, true, true);
    else          itemList.deselectAllRows();
    updateProperties();
}

void ScenePanel::updateProperties()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    auto* item = selected();
    const auto type = item != nullptr ? item->type : SourceType::decks;
    const bool any = item != nullptr;
    const bool isText = any && type == SourceType::text;
    textEditor.setVisible (isText);
    fontSize.setVisible (isText);
    fontSizeLabel.setVisible (isText);
    boldToggle.setVisible (isText);
    shadowToggle.setVisible (isText);
    colourButton.setVisible (any && (type == SourceType::text || type == SourceType::colour));
    fileButton.setVisible (any && type == SourceType::image);
    const bool isCapture = any && (type == SourceType::screen || type == SourceType::window);
    captureTarget.setVisible (isCapture);
    cursorToggle.setVisible (isCapture);
    captureStatus.setVisible (isCapture);
    refreshTargets.setVisible (isCapture && type == SourceType::window);
    clientToggle.setVisible (isCapture && type == SourceType::window);
    for (auto* c : std::initializer_list<juce::Component*> { &opacity, &opacityLabel, &rotation, &rotationLabel, &cropLabel,
                                                              &cropLeft, &cropTop, &cropRight, &cropBottom, &fitButton, &resetButton })
        c->setVisible (any);
    if (any)
    {
        if (textEditor.getText() != item->text)
            textEditor.setText (item->text, false);
        fontSize.setValue (item->fontSize, juce::dontSendNotification);
        boldToggle.setToggleState (item->bold, juce::dontSendNotification);
        shadowToggle.setToggleState (item->shadow, juce::dontSendNotification);
        cursorToggle.setToggleState (item->cursor, juce::dontSendNotification);
        clientToggle.setToggleState (item->clientArea, juce::dontSendNotification);
        if (isCapture)
            fillCaptureTargets();
        opacity.setValue (item->opacity * 100.0, juce::dontSendNotification);
        rotation.setValue (item->rotation, juce::dontSendNotification);
        const auto natural = program.naturalSize (*item);
        for (auto* s : { &cropLeft, &cropRight })
            s->setRange (0.0, juce::jmax (1.0, natural.x * 0.95), 1.0);
        for (auto* s : { &cropTop, &cropBottom })
            s->setRange (0.0, juce::jmax (1.0, natural.y * 0.95), 1.0);
        cropLeft.setValue (item->cropLeft, juce::dontSendNotification);
        cropTop.setValue (item->cropTop, juce::dontSendNotification);
        cropRight.setValue (item->cropRight, juce::dontSendNotification);
        cropBottom.setValue (item->cropBottom, juce::dontSendNotification);
        propsLabel.setText ("PROPIEDADES: " + item->name.toUpperCase(), juce::dontSendNotification);
    }
    else
    {
        propsLabel.setText ("PROPIEDADES (elige una fuente)", juce::dontSendNotification);
    }
    resized();
}

void ScenePanel::addItem (SourceType type)
{
    auto* scene = currentScene();
    if (scene == nullptr)
        return;
    auto item = scenes.makeItem (type, width, height);
    if (type == SourceType::screen)
    {
        // La pantalla principal (antes de publicar: asi no se crea una captura de mas).
        monitorList = ScreenCapture::monitors();
        for (size_t m = 0; m < monitorList.size(); ++m)
            if (monitorList[m].primary)
            {
                item.monitor = (int) m;
                item.monitorId = monitorList[m].id;
            }
    }
    scene->items.push_back (item);
    selectedItem = item.id;
    changed();
    setSelectedItem (selectedItem);
    if (onItemSelected)
        onItemSelected (selectedItem);
    if (type == SourceType::image)
        chooseImage();
    if (type == SourceType::screen)
        fitSelectedToScreen();   // encajada en el programa (ya se sabe su tamano)
    if (type == SourceType::window)
        captureTarget.showPopup();   // que elija cual
}

void ScenePanel::deleteSelectedItem()
{
    auto* scene = currentScene();
    if (scene == nullptr || selected() == nullptr)
        return;
    const int id = selectedItem;
    std::erase_if (scene->items, [id] (const SceneItem& i) { return i.id == id; });
    selectedItem = 0;
    changed();
    setSelectedItem (0);
    if (onItemSelected)
        onItemSelected (0);
}

void ScenePanel::moveSelected (int delta)
{
    auto* scene = currentScene();
    if (scene == nullptr)
        return;
    auto& items = scene->items;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].id == selectedItem)
        {
            const int j = (int) i + delta;   // +1 = mas arriba (se ve encima)
            if (juce::isPositiveAndBelow (j, (int) items.size()))
            {
                std::swap (items[i], items[(size_t) j]);
                changed();
                setSelectedItem (selectedItem);
            }
            return;
        }
}

void ScenePanel::fitSelectedToScreen()
{
    auto* item = selected();
    if (item == nullptr)
        return;
    const auto natural = program.naturalSize (*item);
    const float cw = juce::jmax (1.0f, (float) natural.x - item->cropLeft - item->cropRight);
    const float ch = juce::jmax (1.0f, (float) natural.y - item->cropTop - item->cropBottom);
    const auto fit = Compositor::fitInside (cw, ch, { (float) width, (float) height });
    item->position = fit.getPosition();
    item->scaleX = item->scaleY = fit.getWidth() / cw;
    item->rotation = 0.0f;
    changed();
    updateProperties();
}

void ScenePanel::resetSelected()
{
    auto* item = selected();
    if (item == nullptr)
        return;
    item->position = {};
    item->scaleX = item->scaleY = 1.0f;
    item->rotation = 0.0f;
    item->opacity = 1.0f;
    item->cropLeft = item->cropTop = item->cropRight = item->cropBottom = 0.0f;
    changed();
    updateProperties();
}

void ScenePanel::chooseColour()
{
    auto* item = selected();
    if (item == nullptr)
        return;
    const int id = item->id;
    auto picker = std::make_unique<ColourPicker> (item->colour, [this, id] (juce::Colour c)
    {
        if (auto* i = scenes.findItem (id))
        {
            i->colour = c;
            changed();
        }
    });
    juce::CallOutBox::launchAsynchronously (std::move (picker), colourButton.getScreenBounds(), nullptr);
}

void ScenePanel::chooseImage()
{
    const int id = selectedItem;
    chooser = std::make_unique<juce::FileChooser> ("Imagen", juce::File::getSpecialLocation (juce::File::userPicturesDirectory),
                                                   "*.png;*.jpg;*.jpeg;*.gif");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, id] (const juce::FileChooser& fc)
    {
        auto* item = scenes.findItem (id);
        if (item == nullptr || fc.getResult() == juce::File())
            return;
        item->file = fc.getResult();
        item->name = "Imagen";
        changed();   // se carga: ya se sabe su tamano
        // Centrada a su tamano; si no cabe, encajada.
        if ((item = scenes.findItem (id)) != nullptr)
        {
            const auto natural = program.naturalSize (*item);
            const float scale = juce::jmin (1.0f, (float) width / (float) natural.x, (float) height / (float) natural.y);
            item->scaleX = item->scaleY = scale;
            item->position = { ((float) width - (float) natural.x * scale) * 0.5f, ((float) height - (float) natural.y * scale) * 0.5f };
            changed();
            updateProperties();
        }
    });
}

// La lista de pantallas o de ventanas del elemento elegido (id = posicion + 1; la ventana guardada
// que ya no esta abierta sale arriba con id = 10000).
void ScenePanel::fillCaptureTargets()
{
    auto* item = selected();
    if (item == nullptr)
        return;
    const juce::ScopedValueSetter<bool> svs (updating, true);
    captureTarget.clear (juce::dontSendNotification);
    if (item->type == SourceType::screen)
    {
        monitorList = ScreenCapture::monitors();
        for (size_t m = 0; m < monitorList.size(); ++m)
            captureTarget.addItem (ScreenCapture::describe (monitorList[m], (int) m), (int) m + 1);
        const int index = ScreenCapture::findMonitor (monitorList, item->monitorId, item->monitor);
        captureTarget.setSelectedId (index + 1, juce::dontSendNotification);
        return;
    }
    windowList = ScreenCapture::windows();
    int chosen = 0;
    for (size_t w = 0; w < windowList.size(); ++w)
    {
        const auto& win = windowList[w];
        captureTarget.addItem (ScreenCapture::describe (win), (int) w + 1);
        if (chosen == 0 && win.title == item->windowTitle && win.className == item->windowClass && win.exe.equalsIgnoreCase (item->windowExe))
            chosen = (int) w + 1;
    }
    if (chosen == 0 && item->windowTitle.isNotEmpty())
    {
        // Guardada pero no abierta ahora (o con otro titulo): se muestra igual.
        ScreenCapture::Window saved { nullptr, item->windowTitle, item->windowClass, item->windowExe };
        captureTarget.addItem (ScreenCapture::describe (saved), 10000);
        chosen = 10000;
    }
    captureTarget.setSelectedId (chosen, juce::dontSendNotification);
}

void ScenePanel::chooseCaptureTarget()
{
    auto* item = selected();
    const int index = captureTarget.getSelectedId() - 1;
    if (item == nullptr || index < 0)
        return;
    bool first = false;
    if (item->type == SourceType::screen && juce::isPositiveAndBelow (index, (int) monitorList.size()))
    {
        item->monitor = index;
        item->monitorId = monitorList[(size_t) index].id;
    }
    else if (item->type == SourceType::window && juce::isPositiveAndBelow (index, (int) windowList.size()))
    {
        const auto& win = windowList[(size_t) index];
        first = item->windowTitle.isEmpty();
        item->windowTitle = win.title;
        item->windowClass = win.className;
        item->windowExe = win.exe;
    }
    else
    {
        return;
    }
    changed();
    if (first)
        fitSelectedToScreen();   // la primera ventana elegida se encaja en el programa
    timerCallback();
}

void ScenePanel::timerCallback()
{
    auto* item = selected();
    if (item == nullptr || (item->type != SourceType::screen && item->type != SourceType::window))
        return;
    juce::String text;
    if (auto capture = program.captureFor (item->id))
    {
        text = capture->getStatus();
        if (text.isEmpty())
        {
            const auto size = capture->getSize();
            text = size.x > 0 ? "Capturando " + juce::String (size.x) + "x" + juce::String (size.y) + " (" + capture->getMethod() + ")" : "Esperando imagen...";
        }
    }
    captureStatus.setColour (juce::Label::textColourId, text.startsWith ("Capturando") ? juce::Colours::grey : juce::Colour (0xffffb74d));
    if (captureStatus.getText() != text)
        captureStatus.setText (text, juce::dontSendNotification);
}

void ScenePanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1e24));
}

void ScenePanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto row = [&r] (int h) { auto a = r.removeFromTop (h); r.removeFromTop (4); return a; };
    auto buttons = [&] (std::initializer_list<juce::Component*> list)
    {
        auto a = row (24);
        const int w = a.getWidth() / (int) list.size();
        for (auto* c : list)
            c->setBounds (a.removeFromLeft (w).reduced (1, 0));
    };

    scenesLabel.setBounds (row (16));
    sceneList.setBounds (row (76));
    buttons ({ &addScene, &removeScene, &renameScene });
    auto t = row (24);
    transitionType.setBounds (t.removeFromLeft (t.getWidth() / 2 - 2));
    t.removeFromLeft (4);
    transitionMs.setBounds (t);
    r.removeFromTop (4);

    itemsLabel.setBounds (row (16));
    itemList.setBounds (row (100));
    buttons ({ &addItemButton, &removeItem, &moveUp, &moveDown });
    r.removeFromTop (4);

    propsLabel.setBounds (row (16));
    auto labelled = [&] (juce::Label& l, juce::Component& c)
    {
        auto a = row (22);
        l.setBounds (a.removeFromLeft (70));
        c.setBounds (a);
    };
    if (textEditor.isVisible())
    {
        textEditor.setBounds (row (48));
        labelled (fontSizeLabel, fontSize);
        auto a = row (22);
        boldToggle.setBounds (a.removeFromLeft (a.getWidth() / 2));
        shadowToggle.setBounds (a);
    }
    if (colourButton.isVisible())
        colourButton.setBounds (row (24));
    if (fileButton.isVisible())
        fileButton.setBounds (row (24));
    if (captureTarget.isVisible())
    {
        auto a = row (24);
        if (refreshTargets.isVisible())
        {
            refreshTargets.setBounds (a.removeFromRight (80));
            a.removeFromRight (4);
        }
        captureTarget.setBounds (a);
        cursorToggle.setBounds (row (22));
        if (clientToggle.isVisible())
            clientToggle.setBounds (row (22));
        captureStatus.setBounds (row (18));
    }
    if (opacity.isVisible())
    {
        labelled (opacityLabel, opacity);
        labelled (rotationLabel, rotation);
        cropLabel.setBounds (row (16));
        auto a = row (20);
        const int w = a.getWidth() / 4;
        for (auto* s : { &cropLeft, &cropTop, &cropRight, &cropBottom })
            s->setBounds (a.removeFromLeft (w).reduced (2, 0));
        buttons ({ &fitButton, &resetButton });
    }
}
}
