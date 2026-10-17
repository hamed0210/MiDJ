#include "UI/VideoWindow.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>

namespace midj
{
namespace
{
    // La ventana hija donde Direct3D presenta. Es transparente al raton (HTTRANSPARENT): los
    // clics llegan a la ventana de JUCE que la contiene. No se pinta nada: lo hace el compositor.
    LRESULT CALLBACK surfaceProc (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
            case WM_NCHITTEST:  return HTTRANSPARENT;
            case WM_ERASEBKGND: return 1;
            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                BeginPaint (hwnd, &ps);
                EndPaint (hwnd, &ps);
                return 0;
            }
            default: break;
        }
        return DefWindowProcW (hwnd, message, wParam, lParam);
    }

    HWND createSurface()
    {
        static const ATOM windowClass = []
        {
            WNDCLASSEXW wc {};
            wc.cbSize = sizeof (wc);
            wc.style = CS_OWNDC;
            wc.lpfnWndProc = surfaceProc;
            wc.hInstance = GetModuleHandleW (nullptr);
            wc.hCursor = nullptr;   // el cursor lo pone JUCE (oculto en pantalla completa)
            wc.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
            wc.lpszClassName = L"MiDJProjectorSurface";
            return RegisterClassExW (&wc);
        }();
        if (windowClass == 0)
            return nullptr;
        // Se crea suelta; HWNDComponent la convierte en hija de la ventana de JUCE.
        return CreateWindowExW (0, L"MiDJProjectorSurface", L"", WS_POPUP | WS_CLIPSIBLINGS, 0, 0, 16, 16,
                                nullptr, nullptr, GetModuleHandleW (nullptr), nullptr);
    }
}

// --- Superficie --------------------------------------------------------------------------------

ProjectorView::ProjectorView (Compositor& c) : compositor (c)
{
    setOpaque (true);
    surface.setInterceptsMouseClicks (false, false);
    if (auto* hwnd = createSurface())
    {
        surface.setHWND (hwnd);
        compositor.addOutput (hwnd);
    }
    addAndMakeVisible (surface);
}

ProjectorView::~ProjectorView()
{
    // Primero se suelta la cadena de intercambio y luego se destruye la ventana.
    if (auto* hwnd = surface.getHWND())
        compositor.removeOutput (hwnd);
    surface.setHWND (nullptr);
}

void ProjectorView::resized() { surface.setBounds (getLocalBounds()); }
void ProjectorView::paint (juce::Graphics& g) { g.fillAll (juce::Colours::black); }

juce::Rectangle<float> ProjectorView::programArea() const
{
    return Compositor::fitInside ((float) compositor.getWidth(), (float) compositor.getHeight(), getLocalBounds().toFloat());
}

juce::Point<float> ProjectorView::toProgram (juce::Point<float> p) const
{
    const auto area = programArea();
    if (area.isEmpty())
        return {};
    return { (p.x - area.getX()) * (float) compositor.getWidth() / area.getWidth(),
             (p.y - area.getY()) * (float) compositor.getHeight() / area.getHeight() };
}

void ProjectorView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onRightClick)
            onRightClick (e);
        return;
    }
    if (onMouseDown)
        onMouseDown (e);
}

void ProjectorView::mouseDrag (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && onMouseDrag)
        onMouseDrag (e);
}

void ProjectorView::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && onMouseUp)
        onMouseUp (e);
}

void ProjectorView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && onDoubleClick)
        onDoubleClick();
}

// --- Ventana de vista previa -----------------------------------------------------------------

class VideoStage::PreviewWindow : public juce::DocumentWindow
{
public:
    PreviewWindow (VideoStage& s)
        : juce::DocumentWindow ("MiDJ - Video", juce::Colours::black, juce::DocumentWindow::allButtons), stage (s)
    {
        auto* content = new Content (stage);
        content->setSize (980, 640);
        setContentOwned (content, true);
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setResizeLimits (640, 480, 8192, 8192);
        setWantsKeyboardFocus (true);
    }

    ProjectorView& getView() { return dynamic_cast<Content*> (getContentComponent())->view; }
    ScenePanel& getPanel() { return dynamic_cast<Content*> (getContentComponent())->panel; }

    void closeButtonPressed() override
    {
        stage.showPreview (false);
        if (stage.onPreviewClosed)
            stage.onPreviewClosed();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        return stage.previewKey (key) || juce::DocumentWindow::keyPressed (key);
    }

private:
    // La vista previa a la izquierda y el panel de escenas a la derecha.
    struct Content : public juce::Component
    {
        explicit Content (VideoStage& s)
            : view (s.compositor), panel (s.scenes, s.program, s.compositor.getWidth(), s.compositor.getHeight())
        {
            view.onDoubleClick = [&s] { s.toggleProjector (VideoStage::audienceDisplay()); };
            view.onRightClick = [&s] (const juce::MouseEvent&) { s.showMenu(); };
            view.onMouseDown = [&s] (const juce::MouseEvent& e) { s.previewMouseDown (e); };
            view.onMouseDrag = [&s] (const juce::MouseEvent& e) { s.previewMouseDrag (e); };
            view.onMouseUp = [&s] (const juce::MouseEvent& e) { s.previewMouseUp (e); };
            panel.onChanged = [&s] { s.scenesChanged(); };
            panel.onSceneChosen = [&s] (int id) { s.switchToScene (id); };
            panel.onItemSelected = [&s] (int id) { s.selectItem (id); };
            addAndMakeVisible (view);
            addAndMakeVisible (panel);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            panel.setBounds (r.removeFromRight (320));
            view.setBounds (r);
        }

        ProjectorView view;
        ScenePanel panel;
    };

    VideoStage& stage;
};

// --- Proyector a pantalla completa -----------------------------------------------------------

class VideoStage::FullScreenProjector : public juce::Component
{
public:
    FullScreenProjector (VideoStage& s, juce::Rectangle<int> displayArea)
        : stage (s), area (displayArea), view (stage.compositor)
    {
        // Como los proyectores de OBS: sin borde ni barra, cubre el monitor y sin cursor.
        setOpaque (true);
        setMouseCursor (juce::MouseCursor::NoCursor);
        view.setMouseCursor (juce::MouseCursor::NoCursor);
        view.onDoubleClick = [this] { closeLater(); };
        view.onRightClick = [this] (const juce::MouseEvent&)
        {
            juce::PopupMenu menu;
            menu.addItem ("Cerrar proyector", [this] { closeLater(); });
            menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
        };
        addAndMakeVisible (view);
        setWantsKeyboardFocus (true);
        setBounds (area);
        addToDesktop (juce::ComponentPeer::windowAppearsOnTaskbar);
        setVisible (true);
        toFront (true);
    }

    juce::Rectangle<int> getArea() const { return area; }
    void resized() override { view.setBounds (getLocalBounds()); }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            closeLater();
            return true;
        }
        return false;
    }

private:
    // No se puede borrar el componente dentro de su propio evento: se cierra justo despues.
    void closeLater()
    {
        juce::Component::SafePointer<FullScreenProjector> self (this);
        juce::MessageManager::callAsync ([self]
        {
            if (self != nullptr)
                self->stage.closeProjectorAt (self->area);
        });
    }

    VideoStage& stage;
    juce::Rectangle<int> area;
    ProjectorView view;
};

// --- Escenario ---------------------------------------------------------------------------------

VideoStage::VideoStage (std::array<DeckVideo*, 2> v, std::array<const Deck*, 2> d, const Mixer& mixer,
                        Compositor::Settings settings, juce::File file)
    : compositor (settings), captures (compositor.getDevice()), mix (v, d, mixer), program (mix, compositor.getWidth(), compositor.getHeight()),
      videos (v), decks (d), scenesFile (std::move (file))
{
    mix.setProgramSize (compositor.getWidth(), compositor.getHeight());
    const auto xml = juce::XmlDocument::parse (scenesFile);
    scenes = xml != nullptr ? SceneCollection::fromXml (*xml, compositor.getWidth(), compositor.getHeight())
                            : SceneCollection::createDefault (compositor.getWidth(), compositor.getHeight());
    program.setCaptures (&captures);
    program.setScenes (scenes);
    compositor.buildFrame = [this] (Compositor::Frame& frame) { program.buildFrame (frame); };
}

VideoStage::~VideoStage()
{
    stopTimer();
    if (dirty)
        save();
    compositor.stop();
    projectors.clear();
    preview.reset();
    for (auto* video : videos)
        video->setActive (false);
}

void VideoStage::save()
{
    dirty = false;
    if (auto xml = scenes.toXml())
        xml->writeTo (scenesFile);
}

void VideoStage::scenesChanged()
{
    program.setScenes (scenes);
    dirty = true;
    lastChange = juce::Time::getMillisecondCounter();
    if (selectedItem() == nullptr)
        selected = 0;
    updateOverlay();
}

void VideoStage::switchToScene (int sceneId)
{
    if (scenes.findScene (sceneId) == nullptr)
        return;
    scenes.currentScene = sceneId;
    program.setScenes (scenes);
    program.switchTo (sceneId);
    dirty = true;
    lastChange = juce::Time::getMillisecondCounter();
    selected = 0;
    if (preview != nullptr)
        preview->getPanel().refresh();
    updateOverlay();
}

SceneItem* VideoStage::selectedItem()
{
    if (auto* scene = scenes.findScene (scenes.currentScene))
        for (auto& i : scene->items)
            if (i.id == selected)
                return &i;
    return nullptr;
}

void VideoStage::selectItem (int itemId)
{
    selected = itemId;
    if (preview != nullptr && preview->getPanel().getSelectedItem() != itemId)
        preview->getPanel().setSelectedItem (itemId);
    updateOverlay();
}

void VideoStage::updateOverlay()
{
    if (preview == nullptr)
        return;
    std::vector<Compositor::Quad> boxes;
    if (auto* item = selectedItem())
        boxes.push_back (SceneGeometry::quad (*item, program.naturalSize (*item)));
    compositor.setOverlay (preview->getView().getSurface(), std::move (boxes));
}

void VideoStage::previewMouseDown (const juce::MouseEvent& e)
{
    auto& view = preview->getView();
    const auto p = view.toProgram (e.position);
    drag = Drag::none;
    preview->grabKeyboardFocus();

    // Una esquina del elemento elegido: escalar (la de enfrente queda fija).
    if (auto* item = selectedItem())
    {
        const auto q = SceneGeometry::quad (*item, program.naturalSize (*item));
        const float grab = 10.0f * (float) compositor.getWidth() / juce::jmax (1.0f, view.programArea().getWidth());
        for (int c = 0; c < 4; ++c)
            if (q[(size_t) c].getDistanceFrom (p) <= grab)
            {
                drag = Drag::scale;
                dragCorner = c;
                dragAnchor = q[(size_t) ((c + 2) % 4)];
                return;
            }
    }
    // Si no, el de mas arriba bajo el raton: elegirlo y moverlo.
    int hit = 0;
    if (auto* scene = scenes.findScene (scenes.currentScene))
        for (auto it = scene->items.rbegin(); it != scene->items.rend(); ++it)
            if (it->visible && SceneGeometry::contains (*it, program.naturalSize (*it), p))
            {
                hit = it->id;
                dragOffset = p - it->position;
                drag = Drag::move;
                break;
            }
    selectItem (hit);
}

void VideoStage::previewMouseDrag (const juce::MouseEvent& e)
{
    auto* item = selectedItem();
    if (item == nullptr || drag == Drag::none)
        return;
    const auto p = preview->getView().toProgram (e.position);
    const auto natural = program.naturalSize (*item);
    const float W = (float) compositor.getWidth(), H = (float) compositor.getHeight();
    if (drag == Drag::move)
    {
        // Se pega a los bordes y al centro del programa (como OBS) a menos de 12 pixeles.
        auto pos = p - dragOffset;
        const auto b = SceneGeometry::box (*item, natural);
        auto snap = [] (float v, float size, float limit)
        {
            for (float target : { 0.0f, limit - size, (limit - size) * 0.5f })
                if (std::abs (v - target) < 12.0f)
                    return target;
            return v;
        };
        if (! e.mods.isShiftDown() && item->rotation == 0.0f)
        {
            pos.x = snap (pos.x, b.getWidth(), W);
            pos.y = snap (pos.y, b.getHeight(), H);
        }
        item->position = pos;
    }
    else
    {
        // En el espacio del elemento sin girar: el raton respecto de la esquina fija.
        const float r = juce::degreesToRadians (item->rotation);
        auto local = (p - dragAnchor).rotatedAboutOrigin (-r);
        const float sx = (dragCorner == 1 || dragCorner == 2) ? 1.0f : -1.0f;   // la esquina arrastrada esta a la derecha
        const float sy = (dragCorner >= 2) ? 1.0f : -1.0f;                     // o abajo
        const float cw = juce::jmax (1.0f, (float) natural.x - item->cropLeft - item->cropRight);
        const float ch = juce::jmax (1.0f, (float) natural.y - item->cropTop - item->cropBottom);
        float w = juce::jmax (8.0f, local.x * sx), h = juce::jmax (8.0f, local.y * sy);
        if (! e.mods.isShiftDown())
        {
            // Sin deformar (Mayus para deformar), como OBS.
            const float scale = juce::jmax (w / cw, h / ch);
            w = cw * scale;
            h = ch * scale;
        }
        // El centro nuevo, girado, desde la esquina fija.
        const auto centre = dragAnchor + juce::Point<float> (sx * w * 0.5f, sy * h * 0.5f).rotatedAboutOrigin (r);
        item->scaleX = w / cw;
        item->scaleY = h / ch;
        item->position = { centre.x - w * 0.5f, centre.y - h * 0.5f };
    }
    scenesChanged();
}

void VideoStage::previewMouseUp (const juce::MouseEvent&)
{
    if (drag != Drag::none && preview != nullptr)
        preview->getPanel().refresh();
    drag = Drag::none;
}

bool VideoStage::previewKey (const juce::KeyPress& key)
{
    auto* item = selectedItem();
    if (item == nullptr)
        return false;
    if (key == juce::KeyPress::deleteKey)
    {
        preview->getPanel().deleteSelectedItem();
        return true;
    }
    // Flechas: mover 1 pixel (10 con Mayus).
    const float step = key.getModifiers().isShiftDown() ? 10.0f : 1.0f;
    juce::Point<float> delta;
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::leftKey)       delta = { -step, 0.0f };
    else if (code == juce::KeyPress::rightKey) delta = { step, 0.0f };
    else if (code == juce::KeyPress::upKey)    delta = { 0.0f, -step };
    else if (code == juce::KeyPress::downKey)  delta = { 0.0f, step };
    else return false;
    item->position += delta;
    scenesChanged();
    return true;
}

NowPlaying VideoStage::nowPlaying() const
{
    // La del deck que mas se ve segun el crossfader (si hay empate, el que suena; si no, A).
    std::array<juce::String, 2> titles;
    for (int d = 0; d < 2; ++d)
        titles[(size_t) d] = getTitle != nullptr ? getTitle (d) : juce::String();
    const auto w = VideoMix::weights (mix.getCrossfader(), titles[0].isNotEmpty(), titles[1].isNotEmpty());
    int d = w[1] > w[0] ? 1 : 0;
    if (w[0] == w[1] && w[0] > 0.0f && ! decks[0]->isPlaying() && decks[1]->isPlaying())
        d = 1;
    NowPlaying now;
    const auto& t = titles[(size_t) d];
    if (t.contains (" - "))
    {
        now.artist = t.upToFirstOccurrenceOf (" - ", false, false);
        now.title = t.fromFirstOccurrenceOf (" - ", false, false);
    }
    else
    {
        now.title = t;
    }
    if (t.isNotEmpty())
        now.bpm = std::round (decks[(size_t) d]->getCurrentBpm() * 10.0) / 10.0;
    return now;
}

void VideoStage::showPreview (bool shouldShow)
{
    if (shouldShow && preview == nullptr)
    {
        preview = std::make_unique<PreviewWindow> (*this);
        preview->centreWithSize (980, 640 + 32);
    }
    if (preview != nullptr)
    {
        preview->setVisible (shouldShow);
        if (shouldShow)
            preview->toFront (true);
    }
    updateRunning();
}

bool VideoStage::isPreviewShowing() const
{
    return preview != nullptr && preview->isVisible();
}

int VideoStage::audienceDisplay()
{
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    for (int i = 0; i < displays.size(); ++i)
        if (! displays.getReference (i).isMain)
            return i;
    return 0;
}

bool VideoStage::hasProjector (int displayIndex) const
{
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    if (! juce::isPositiveAndBelow (displayIndex, displays.size()))
        return false;
    const auto area = displays.getReference (displayIndex).totalArea;
    for (const auto& p : projectors)
        if (p->getArea() == area)
            return true;
    return false;
}

void VideoStage::toggleProjector (int displayIndex)
{
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    if (! juce::isPositiveAndBelow (displayIndex, displays.size()))
        return;
    const auto area = displays.getReference (displayIndex).totalArea;
    if (hasProjector (displayIndex))
    {
        closeProjectorAt (area);
        return;
    }
    projectors.push_back (std::make_unique<FullScreenProjector> (*this, area));
    projectors.back()->grabKeyboardFocus();
    updateRunning();
}

void VideoStage::closeProjectorAt (juce::Rectangle<int> area)
{
    std::erase_if (projectors, [area] (const auto& p) { return p->getArea() == area; });
    updateRunning();
}

void VideoStage::closeProjectors()
{
    projectors.clear();
    updateRunning();
}

void VideoStage::updateRunning()
{
    const bool anyOpen = isPreviewShowing() || ! projectors.empty();
    // Nadie mira: no se compone ni se decodifica (no se gasta CPU ni GPU).
    for (auto* video : videos)
        video->setActive (anyOpen);
    if (anyOpen)
    {
        timerCallback();   // rotulos al dia antes del primer fotograma
        startTimerHz (10);
        compositor.start();
    }
    else
    {
        stopTimer();
        compositor.stop();
    }
}

void VideoStage::timerCallback()
{
    for (int d = 0; d < 2; ++d)
        mix.setTitle (d, getTitle != nullptr ? getTitle (d) : juce::String());
    captures.tick();                        // capturas perdidas (ventana cerrada y vuelta a abrir...)
    program.setNowPlaying (nowPlaying());   // los textos con {titulo} se rehacen solo si cambia
    updateOverlay();                        // (y un texto que cambia cambia de tamano)
    // Se guarda un segundo despues del ultimo cambio (no a cada movimiento del raton).
    if (dirty && drag == Drag::none && juce::Time::getMillisecondCounter() - lastChange > 1000)
        save();

    // Un monitor desconectado: su proyector se cierra.
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    const auto before = projectors.size();
    std::erase_if (projectors, [&displays] (const auto& p)
    {
        for (const auto& d : displays)
            if (d.totalArea == p->getArea())
                return false;
        return true;
    });
    if (projectors.size() != before)
        updateRunning();
}

juce::String VideoStage::statusText() const
{
    const auto s = compositor.getStats();
    return (s.hardware ? s.adapter : juce::String ("Sin GPU (WARP)")) + "  -  "
           + juce::String (s.renderMs, 1) + " ms por fotograma, " + juce::String ((juce::int64) s.lagged) + " perdidos";
}

void VideoStage::showMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Programa " + juce::String (compositor.getWidth()) + "x" + juce::String (compositor.getHeight())
                           + " a " + juce::String (compositor.getFps()) + " fps");
    menu.addItem (statusText(), false, false, nullptr);
    menu.addSeparator();
    juce::PopupMenu screens;
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    for (int i = 0; i < displays.size(); ++i)
    {
        const auto& d = displays.getReference (i);
        const auto px = (d.totalArea.toDouble() * d.scale).toNearestInt();
        screens.addItem ("Pantalla " + juce::String (i + 1) + ": " + juce::String (px.getWidth()) + "x" + juce::String (px.getHeight())
                             + (d.isMain ? " (principal)" : ""),
                         true, hasProjector (i), [this, i] { toggleProjector (i); });
    }
    menu.addSubMenu ("Proyector a pantalla completa", screens);
    menu.addItem ("Cerrar proyectores", ! projectors.empty(), false, [this] { closeProjectors(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}
}
