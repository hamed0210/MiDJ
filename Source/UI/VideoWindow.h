#pragma once

#include "UI/ScenePanel.h"
#include "Video/Compositor.h"
#include "Video/VideoMix.h"
#include "Video/VideoProgram.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace midj
{
// Una superficie donde el compositor dibuja el programa (una ventana hija de Windows con su
// cadena de intercambio de Direct3D). Los clics pasan a traves de ella al componente de JUCE.
class ProjectorView : public juce::Component
{
public:
    explicit ProjectorView (Compositor& compositor);
    ~ProjectorView() override;

    std::function<void()> onDoubleClick;
    std::function<void (const juce::MouseEvent&)> onRightClick;
    // Clic izquierdo y arrastre (para mover y escalar elementos en la vista previa).
    std::function<void (const juce::MouseEvent&)> onMouseDown, onMouseDrag, onMouseUp;

    void* getSurface() const { return surface.getHWND(); }
    // Donde se ve el programa dentro de la vista (con bandas negras) y el paso a pixeles del programa.
    juce::Rectangle<float> programArea() const;
    juce::Point<float> toProgram (juce::Point<float> viewPosition) const;

    void resized() override;
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    Compositor& compositor;
    juce::HWNDComponent surface;
};

// La salida de video de MiDJ, como OBS: el compositor (un fotograma a ritmo fijo en la GPU), las
// escenas, la ventana de vista previa con el panel de escenas y los proyectores a pantalla
// completa, uno por monitor. Solo se compone y se decodifica mientras hay alguna ventana abierta.
class VideoStage : private juce::Timer
{
public:
    VideoStage (std::array<DeckVideo*, 2> videos, std::array<const Deck*, 2> decks, const Mixer& mixer,
                Compositor::Settings settings, juce::File scenesFile);
    ~VideoStage() override;

    std::function<juce::String (int deckIndex)> getTitle;   // "Artista - Titulo" de la cancion de un deck
    std::function<void()> onPreviewClosed;                     // el usuario cerro la vista previa

    bool isReady() const { return compositor.isReady(); }
    juce::String getError() const { return compositor.getError(); }

    void showPreview (bool shouldShow);
    bool isPreviewShowing() const;
    // Proyector a pantalla completa en un monitor (indice de Displays); de nuevo lo cierra.
    void toggleProjector (int displayIndex);
    bool hasProjector (int displayIndex) const;
    void closeProjectors();
    // El monitor que no es el principal (el del publico); si solo hay uno, ese.
    static int audienceDisplay();

    // Escenas (hilo de interfaz).
    SceneCollection& getScenes() { return scenes; }
    void switchToScene (int sceneId);
    void scenesChanged();   // tras editar la coleccion: se publica, se refresca y se guarda

    Compositor& getCompositor() { return compositor; }
    void showMenu();   // menu de proyectores (clic derecho en la vista previa)

private:
    class PreviewWindow;
    class FullScreenProjector;

    void timerCallback() override;
    void updateRunning();
    void closeProjectorAt (juce::Rectangle<int> area);
    juce::String statusText() const;
    NowPlaying nowPlaying() const;

    // Edicion en la vista previa (como OBS: clic para elegir, arrastrar para mover, esquinas para escalar).
    void selectItem (int itemId);
    void updateOverlay();
    void previewMouseDown (const juce::MouseEvent& e);
    void previewMouseDrag (const juce::MouseEvent& e);
    void previewMouseUp (const juce::MouseEvent& e);
    bool previewKey (const juce::KeyPress& key);
    SceneItem* selectedItem();
    void save();

    Compositor compositor;
    CaptureManager captures;
    VideoMix mix;
    VideoProgram program;
    std::array<DeckVideo*, 2> videos;
    std::array<const Deck*, 2> decks;
    juce::File scenesFile;
    SceneCollection scenes;
    int selected = 0;
    bool dirty = false;
    juce::uint32 lastChange = 0;

    enum class Drag { none, move, scale };
    Drag drag = Drag::none;
    juce::Point<float> dragOffset;        // movimiento: del raton a la esquina del elemento
    juce::Point<float> dragAnchor;        // escala: la esquina opuesta, fija
    int dragCorner = 0;

    std::unique_ptr<PreviewWindow> preview;
    std::vector<std::unique_ptr<FullScreenProjector>> projectors;
};
}
