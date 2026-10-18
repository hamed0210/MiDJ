#include "UI/ScenePanel.h"
#include "UI/DarkLookAndFeel.h"

namespace midj
{
namespace
{
    const juce::Colour live (0xff26262b);   // la escena al aire / la fuente elegida

    void setUpTitle (juce::Label& l)
    {
        l.setFont (DarkLookAndFeel::sans (14.0f, true));
        l.setColour (juce::Label::textColourId, DarkLookAndFeel::text);
    }

    void setUpList (juce::ListBox& list)
    {
        list.setRowHeight (32);
        list.setColour (juce::ListBox::backgroundColourId, DarkLookAndFeel::panel);
        list.setOutlineThickness (0);
    }

    void layOutPanel (juce::Rectangle<int> r, juce::Label& title, juce::ListBox& list, std::initializer_list<juce::Component*> buttons)
    {
        r = r.reduced (12, 10);
        title.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);
        auto bar = r.removeFromBottom (32);
        r.removeFromBottom (4);
        list.setBounds (r);
        for (auto* b : buttons)
            b->setBounds (bar.removeFromLeft (32).reduced (1));
    }

    void askName (juce::Component* parent, const juce::String& heading, const juce::String& current,
                  std::function<void (const juce::String&)> done)
    {
        auto* window = new juce::AlertWindow (heading, {}, juce::MessageBoxIconType::NoIcon, parent);
        window->addTextEditor ("name", current);
        window->addButton ("Aceptar", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([window, done] (int result)
        {
            const auto name = window->getTextEditorContents ("name").trim();
            if (result == 1 && name.isNotEmpty())
                done (name);
        }), true);
    }
}

// --- Escenas ---------------------------------------------------------------------------------

ScenesPanel::ScenesPanel (VideoStage& s) : stage (s)
{
    setUpTitle (title);
    setUpList (list);
    list.setModel (this);
    addAndMakeVisible (title);
    addAndMakeVisible (list);

    addButton.onClick = [this]
    {
        auto& scenes = stage.getScenes();
        auto& scene = scenes.addScene ("Escena " + juce::String (scenes.scenes.size()));
        scene.items.push_back (scenes.makeItem (SourceType::decks, stage.getProgramWidth(), stage.getProgramHeight()));
        const int id = scene.id;
        stage.scenesChanged();
        stage.switchToScene (id);
    };
    removeButton.onClick = [this] { removeCurrent(); };
    renameButton.onClick = [this] { rename (stage.getScenes().currentScene); };
    upButton.onClick = [this] { moveCurrent (-1); };
    downButton.onClick = [this] { moveCurrent (+1); };
    for (auto* b : { &addButton, &removeButton, &renameButton, &upButton, &downButton })
        addAndMakeVisible (b);

    stage.addChangeListener (this);
    refresh();
}

ScenesPanel::~ScenesPanel()
{
    stage.removeChangeListener (this);
    list.setModel (nullptr);
}

void ScenesPanel::refresh()
{
    list.updateContent();
    list.repaint();
    removeButton.setEnabled (stage.getScenes().scenes.size() > 1);
}

void ScenesPanel::removeCurrent()
{
    auto& scenes = stage.getScenes();
    if (scenes.scenes.size() <= 1)
        return;   // siempre queda una
    const int id = scenes.currentScene;
    size_t index = 0;
    for (size_t i = 0; i < scenes.scenes.size(); ++i)
        if (scenes.scenes[i].id == id)
            index = i;
    std::erase_if (scenes.scenes, [id] (const Scene& sc) { return sc.id == id; });
    const int next = scenes.scenes[juce::jmin (index, scenes.scenes.size() - 1)].id;
    stage.scenesChanged();
    stage.switchToScene (next);
}

void ScenesPanel::rename (int sceneId)
{
    auto* scene = stage.getScenes().findScene (sceneId);
    if (scene == nullptr)
        return;
    askName (this, "Renombrar escena", scene->name, [this, sceneId] (const juce::String& name)
    {
        if (auto* sc = stage.getScenes().findScene (sceneId))
        {
            sc->name = name;
            stage.scenesChanged();
        }
    });
}

void ScenesPanel::moveCurrent (int delta)
{
    auto& all = stage.getScenes().scenes;
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].id == stage.getScenes().currentScene)
        {
            const int j = (int) i + delta;
            if (juce::isPositiveAndBelow (j, (int) all.size()))
            {
                std::swap (all[i], all[(size_t) j]);
                stage.scenesChanged();
            }
            return;
        }
}

int ScenesPanel::getNumRows()
{
    return (int) stage.getScenes().scenes.size();
}

void ScenesPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool)
{
    const auto& scenes = stage.getScenes().scenes;
    if (! juce::isPositiveAndBelow (row, (int) scenes.size()))
        return;
    const auto& scene = scenes[(size_t) row];
    const bool onAir = scene.id == stage.getScenes().currentScene;
    if (onAir)
    {
        g.setColour (live);
        g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (1.0f), 8.0f);
    }
    g.setColour (onAir ? juce::Colours::white : DarkLookAndFeel::text.withAlpha (0.85f));
    g.setFont (juce::FontOptions (14.0f));
    // Punto verde: la escena que esta en el programa.
    g.setColour (onAir ? DarkLookAndFeel::good : DarkLookAndFeel::line.brighter (0.2f));
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ 14.0f, h * 0.5f }));
    g.setColour (onAir ? juce::Colours::white : DarkLookAndFeel::text.withAlpha (0.85f));
    g.setFont (DarkLookAndFeel::sans (14.0f, onAir));
    const int autoW = scene.autoWhen != SceneAuto::none ? 22 : 0;
    g.drawText (scene.name, 26, 0, w - 74 - autoW, h, juce::Justification::centredLeft, true);
    // Cambio automatico: nota (audio) o pelicula (video).
    if (autoW > 0)
    {
        const bool video = scene.autoWhen == SceneAuto::video;
        Icons::draw (g, juce::Rectangle<float> ((float) (w - 70), 0.0f, 16.0f, (float) h).withSizeKeepingCentre (13.0f, 13.0f),
                     video ? Icons::Glyph::film : Icons::Glyph::note, video ? juce::Colour (0xffd58cf5) : DarkLookAndFeel::accentA);
    }
    // Atajo de teclado (Ctrl+1-9; tambien F1-F9).
    if (row < 9)
    {
        g.setColour (onAir ? juce::Colours::white.withAlpha (0.6f) : DarkLookAndFeel::textQuiet.withAlpha (0.7f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("Ctrl+" + juce::String (row + 1), w - 50, 0, 44, h, juce::Justification::centredRight, false);
    }
}

void ScenesPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    const auto& scenes = stage.getScenes().scenes;
    if (! juce::isPositiveAndBelow (row, (int) scenes.size()))
        return;
    const int id = scenes[(size_t) row].id;
    stage.switchToScene (id);
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem ("Renombrar", [this, id] { rename (id); });
        menu.addItem ("Duplicar", [this, id]
        {
            auto& sc = stage.getScenes();
            const auto* original = sc.findScene (id);
            if (original == nullptr)
                return;
            auto items = original->items;
            auto& copy = sc.addScene (original->name + " 2");
            for (auto& i : items)
                i.id = sc.newId();   // mismos ajustes: comparte capturas y multimedia
            copy.items = std::move (items);
            const int newId = copy.id;
            stage.scenesChanged();
            stage.switchToScene (newId);
        });
        menu.addItem ("Borrar", stage.getScenes().scenes.size() > 1, false, [this] { removeCurrent(); });
        // Cambio automatico: una sola escena por tipo (al elegirla se quita de la que la tenia).
        juce::PopupMenu autoMenu;
        const auto current = stage.getScenes().findScene (id)->autoWhen;
        auto setAuto = [this, id] (SceneAuto when)
        {
            for (auto& sc : stage.getScenes().scenes)
                if (sc.id == id)
                    sc.autoWhen = when;
                else if (when != SceneAuto::none && sc.autoWhen == when)
                    sc.autoWhen = SceneAuto::none;
            stage.scenesChanged();
        };
        autoMenu.addItem ("Nunca", true, current == SceneAuto::none, [setAuto] { setAuto (SceneAuto::none); });
        autoMenu.addItem ("Cuando suena una cancion de audio", true, current == SceneAuto::audio, [setAuto] { setAuto (SceneAuto::audio); });
        autoMenu.addItem ("Cuando suena un video", true, current == SceneAuto::video, [setAuto] { setAuto (SceneAuto::video); });
        menu.addSeparator();
        menu.addSubMenu ("Pasar a esta escena automaticamente", autoMenu);
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }
}

void ScenesPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    const auto& scenes = stage.getScenes().scenes;
    if (juce::isPositiveAndBelow (row, (int) scenes.size()))
        rename (scenes[(size_t) row].id);
}

void ScenesPanel::paint (juce::Graphics& g)
{
    g.setColour (DarkLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 14.0f);
}

void ScenesPanel::resized()
{
    layOutPanel (getLocalBounds(), title, list, { &addButton, &removeButton, &renameButton, &upButton, &downButton });
}

// --- Fuentes ---------------------------------------------------------------------------------

SourcesPanel::SourcesPanel (VideoStage& s) : stage (s)
{
    setUpTitle (title);
    setUpList (list);
    list.setModel (this);
    addAndMakeVisible (title);
    addAndMakeVisible (list);

    addButton.onClick = [this] { stage.showAddSource(); };
    removeButton.onClick = [this] { if (stage.getSelectedItem() != 0) stage.deleteItem (stage.getSelectedItem()); };
    propertiesButton.onClick = [this] { if (stage.getSelectedItem() != 0) stage.showProperties (stage.getSelectedItem()); };
    upButton.onClick = [this] { moveSelected (+1); };
    downButton.onClick = [this] { moveSelected (-1); };
    for (auto* b : { &addButton, &removeButton, &propertiesButton, &upButton, &downButton })
        addAndMakeVisible (b);

    stage.addChangeListener (this);
    refresh();
}

SourcesPanel::~SourcesPanel()
{
    stage.removeChangeListener (this);
    list.setModel (nullptr);
}

SceneItem* SourcesPanel::itemAt (int row)
{
    auto* scene = stage.getScenes().findScene (stage.getScenes().currentScene);
    if (scene == nullptr || ! juce::isPositiveAndBelow (row, (int) scene->items.size()))
        return nullptr;
    return &scene->items[scene->items.size() - 1 - (size_t) row];   // arriba el que se ve encima
}

void SourcesPanel::refresh()
{
    list.updateContent();
    int row = -1;
    for (int r = 0; r < getNumRows(); ++r)
        if (auto* i = itemAt (r); i != nullptr && i->id == stage.getSelectedItem())
            row = r;
    if (row >= 0) list.selectRow (row, true, true);
    else          list.deselectAllRows();
    list.repaint();
    const bool any = row >= 0;
    for (auto* b : { &removeButton, &propertiesButton, &upButton, &downButton })
        b->setEnabled (any);
}

void SourcesPanel::moveSelected (int delta)
{
    auto* scene = stage.getScenes().findScene (stage.getScenes().currentScene);
    if (scene == nullptr)
        return;
    auto& items = scene->items;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].id == stage.getSelectedItem())
        {
            const int j = (int) i + delta;   // +1 = mas arriba (se ve encima)
            if (juce::isPositiveAndBelow (j, (int) items.size()))
            {
                std::swap (items[i], items[(size_t) j]);
                stage.scenesChanged();
            }
            return;
        }
}

void SourcesPanel::rename (int itemId)
{
    auto* item = stage.getScenes().findItem (itemId);
    if (item == nullptr)
        return;
    askName (this, "Renombrar fuente", item->name, [this, itemId] (const juce::String& name)
    {
        if (auto* i = stage.getScenes().findItem (itemId))
        {
            i->name = name;
            stage.scenesChanged();
        }
    });
}

void SourcesPanel::showItemMenu (int itemId)
{
    auto* item = stage.getScenes().findItem (itemId);
    if (item == nullptr)
        return;
    juce::PopupMenu menu;
    menu.addItem ("Propiedades...", [this, itemId] { stage.showProperties (itemId); });
    menu.addItem ("Renombrar", [this, itemId] { rename (itemId); });
    menu.addSeparator();
    menu.addItem ("Ajustar a la pantalla", [this, itemId] { stage.fitToScreen (itemId); });
    menu.addItem ("Restablecer transformacion", [this, itemId] { stage.resetTransform (itemId); });
    menu.addItem ("Bloquear", true, item->locked, [this, itemId]
    {
        if (auto* i = stage.getScenes().findItem (itemId)) { i->locked = ! i->locked; stage.scenesChanged(); }
    });
    menu.addSeparator();
    menu.addItem ("Eliminar", [this, itemId] { stage.deleteItem (itemId); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

int SourcesPanel::getNumRows()
{
    auto* scene = stage.getScenes().findScene (stage.getScenes().currentScene);
    return scene != nullptr ? (int) scene->items.size() : 0;
}

void SourcesPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool isSelected)
{
    auto* item = itemAt (row);
    if (item == nullptr)
        return;
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
    if (isSelected)
    {
        g.setColour (live);
        g.fillRoundedRectangle (r.reduced (1.0f), 8.0f);
    }
    const auto ink = item->visible ? (isSelected ? juce::Colours::white : DarkLookAndFeel::text) : DarkLookAndFeel::textQuiet;
    Icons::drawSource (g, r.removeFromLeft (26.0f).reduced (4.0f), item->type, ink);
    // A la derecha, como en OBS: ojo y candado.
    auto lock = r.removeFromRight (24.0f);
    auto eye = r.removeFromRight (24.0f);
    Icons::draw (g, eye.reduced (4.0f), item->visible ? Icons::Glyph::eye : Icons::Glyph::eyeOff, ink);
    Icons::draw (g, lock.reduced (5.0f), item->locked ? Icons::Glyph::lock : Icons::Glyph::unlock,
                 item->locked ? ink : ink.withAlpha (0.35f));
    g.setColour (ink);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (item->name, r.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);
}

void SourcesPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    auto* item = itemAt (row);
    if (item == nullptr)
        return;
    const int id = item->id;
    const int w = list.getVisibleRowWidth();
    if (! e.mods.isPopupMenu() && e.x >= w - 48)
    {
        // El ojo y el candado se cambian con un clic.
        if (e.x >= w - 24) item->locked = ! item->locked;
        else               item->visible = ! item->visible;
        stage.selectItem (id);
        stage.scenesChanged();
        return;
    }
    stage.selectItem (id);
    if (e.mods.isPopupMenu())
        showItemMenu (id);
}

void SourcesPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
{
    if (auto* item = itemAt (row); item != nullptr && e.x < list.getVisibleRowWidth() - 48)
        stage.showProperties (item->id);
}

void SourcesPanel::deleteKeyPressed (int)
{
    if (stage.getSelectedItem() != 0)
        stage.deleteItem (stage.getSelectedItem());
}

void SourcesPanel::paint (juce::Graphics& g)
{
    g.setColour (DarkLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 14.0f);
}

void SourcesPanel::resized()
{
    layOutPanel (getLocalBounds(), title, list, { &addButton, &removeButton, &propertiesButton, &upButton, &downButton });
}
}
