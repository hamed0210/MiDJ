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

void ProjectorView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick)
        onRightClick (e);
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
        auto* view = new ProjectorView (stage.compositor);
        view->onDoubleClick = [this] { stage.toggleProjector (VideoStage::audienceDisplay()); };
        view->onRightClick = [this] (const juce::MouseEvent&) { stage.showMenu(); };
        view->setSize (640, 360);
        setContentOwned (view, true);
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setResizeLimits (320, 180, 8192, 8192);
    }

    void closeButtonPressed() override
    {
        stage.showPreview (false);
        if (stage.onPreviewClosed)
            stage.onPreviewClosed();
    }

private:
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

VideoStage::VideoStage (std::array<DeckVideo*, 2> v, std::array<const Deck*, 2> decks, const Mixer& mixer,
                        Compositor::Settings settings)
    : compositor (settings), mix (v, decks, mixer), videos (v)
{
    mix.setProgramSize (compositor.getWidth(), compositor.getHeight());
    compositor.buildFrame = [this] (std::vector<Compositor::Layer>& layers) { mix.buildLayers (layers); };
}

VideoStage::~VideoStage()
{
    stopTimer();
    compositor.stop();
    projectors.clear();
    preview.reset();
    for (auto* video : videos)
        video->setActive (false);
}

void VideoStage::showPreview (bool shouldShow)
{
    if (shouldShow && preview == nullptr)
    {
        preview = std::make_unique<PreviewWindow> (*this);
        preview->centreWithSize (640, 360 + 32);
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
