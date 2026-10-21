#pragma once

#include "Video/Compositor.h"
#include "Video/MediaSources.h"
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
    // solo: muestra el elemento solo del programa (la vista de las propiedades), no el programa.
    explicit ProjectorView (Compositor& compositor, bool solo = false);
    ~ProjectorView() override;

    std::function<void()> onDoubleClick;
    std::function<void (const juce::MouseEvent&)> onRightClick;
    // Clic izquierdo y arrastre (para mover y escalar elementos en la vista previa).
    std::function<void (const juce::MouseEvent&)> onMouseDown, onMouseDrag, onMouseUp;
    std::function<bool (const juce::KeyPress&)> onKey;

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
    bool keyPressed (const juce::KeyPress& key) override;

private:
    Compositor& compositor;
    juce::HWNDComponent surface;
};

class AddSourceWindow;
class SourcePropertiesWindow;

// La salida de video de MiDJ, como OBS: el compositor (un fotograma a ritmo fijo en la GPU), las
// escenas, la vista previa (integrada en la pestana Video del mezclador; tambien una grande para
// editar) y los proyectores a pantalla completa, uno por monitor. Solo se compone y se
// decodifica mientras alguna vista esta a la vista. Avisa (ChangeBroadcaster) cuando cambian
// las escenas o el elemento elegido.
class VideoStage : public juce::ChangeBroadcaster,
                   private juce::Timer
{
public:
    VideoStage (std::array<DeckVideo*, 2> videos, std::array<const Deck*, 2> decks, Mixer& mixer,
                juce::AudioFormatManager& formats, Compositor::Settings settings, juce::File scenesFile);
    ~VideoStage() override;

    std::function<juce::String (int deckIndex)> getTitle;   // "Artista - Titulo" de la cancion de un deck

    bool isReady() const { return compositor.isReady(); }
    juce::String getError() const { return compositor.getError(); }
    int getProgramWidth() const { return compositor.getWidth(); }
    int getProgramHeight() const { return compositor.getHeight(); }
    bool isRunning() const { return running; }   // componiendo (alguna vista a la vista)

    // La vista previa integrada (la coloca el mezclador) y si esta a la vista.
    ProjectorView& getPreview() { return *preview; }
    void setPreviewShowing (bool shouldShow);
    // Una vista previa grande en su ventana, para colocar las fuentes con comodidad.
    void showBigPreview();

    // Proyector a pantalla completa en un monitor (indice de Displays); de nuevo lo cierra.
    void toggleProjector (int displayIndex);
    bool hasProjector (int displayIndex) const;
    void closeProjectors();
    // El monitor que no es el principal (el del publico); si solo hay uno, ese.
    static int audienceDisplay();

    // Escenas (hilo de interfaz).
    SceneCollection& getScenes() { return scenes; }
    const VideoProgram& getProgram() const { return program; }
    VideoMix& getMix() { return mix; }
    void switchToScene (int sceneId);
    // Transiciones propias: anadir (devuelve su id y queda elegida), quitar y probar sin cambiar de escena.
    int addCustomTransition (CustomTransition transition);
    void removeCustomTransition (int id);
    void previewTransition (const CustomTransition& transition);
    void scenesChanged();   // tras editar la coleccion: se publica, se refresca y se guarda

    // El elemento elegido de la escena al aire (0 = ninguno).
    void selectItem (int itemId);
    int getSelectedItem() const { return selected; }
    SceneItem* selectedItem();
    void deleteItem (int itemId);
    // Colocaciones rapidas: encajado en el programa sin deformar, o la posicion de origen.
    void fitToScreen (int itemId);
    void resetTransform (int itemId);
    // Encajarlo en cuanto se sepa su tamano (un video o una capturadora recien elegidos).
    void fitWhenReady (int itemId) { pendingFit.insert (itemId); }

    // Ventanas de fuentes, como las de OBS.
    void showAddSource();
    void showProperties (int itemId);
    void closeDialogs();
    // La vista de la fuente de un elemento sola (para su ventana de propiedades).
    std::unique_ptr<ProjectorView> createSourceView (int itemId);

    // Atajos de la vista previa (Supr, flechas).
    bool previewKey (const juce::KeyPress& key);
    void showMenu();   // menu de proyectores (clic derecho en la vista previa)

private:
    class BigPreview;
    class FullScreenProjector;

    void timerCallback() override;
    void updateRunning();
    void closeProjectorAt (juce::Rectangle<int> area);
    juce::String statusText() const;
    NowPlaying nowPlaying() const;
    void attachEditing (ProjectorView& view);
    // Stinger: su video (se abre al elegir la transicion, para que empiece sin esperas) y arrancarlo.
    void prepareStinger (const CustomTransition* transition);
    void startStinger();

    // Edicion en la vista previa (como OBS: clic para elegir, arrastrar para mover, esquinas para escalar).
    void updateOverlay();
    void previewMouseDown (ProjectorView& view, const juce::MouseEvent& e);
    void previewMouseDrag (ProjectorView& view, const juce::MouseEvent& e);
    void previewMouseUp();
    void save();

    Compositor compositor;
    CaptureManager captures;
    MediaSourceManager media;
    VideoMix mix;
    VideoProgram program;
    std::array<DeckVideo*, 2> videos;
    std::array<const Deck*, 2> decks;
    juce::File scenesFile;
    SceneCollection scenes;
    int selected = 0;
    bool dirty = false, running = false, previewShowing = false;
    juce::uint32 lastChange = 0;
    std::set<int> pendingFit;

    juce::AudioFormatManager& formatManager;
    AuxBus& auxBus;
    juce::TimeSliceThread stingerReadAhead { "MiDJ stinger" };
    std::shared_ptr<MediaFileSource> stinger;   // despues del hilo: se destruye antes
    juce::File stingerFile;
    bool stingerPlaying = false;

    enum class Drag { none, move, scale };
    Drag drag = Drag::none;
    juce::Point<float> dragOffset;        // movimiento: del raton a la esquina del elemento
    juce::Point<float> dragAnchor;        // escala: la esquina opuesta, fija
    int dragCorner = 0;

    std::unique_ptr<ProjectorView> preview;
    std::unique_ptr<BigPreview> bigPreview;
    std::vector<std::unique_ptr<FullScreenProjector>> projectors;
    std::unique_ptr<AddSourceWindow> addSourceWindow;
    std::unique_ptr<SourcePropertiesWindow> propertiesWindow;
};
}
