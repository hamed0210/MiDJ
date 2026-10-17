#pragma once

#include "Video/Compositor.h"
#include "Video/VideoMix.h"

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

    void resized() override;
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    Compositor& compositor;
    juce::HWNDComponent surface;
};

// La salida de video de MiDJ, como los proyectores de OBS: el compositor (un fotograma a ritmo
// fijo en la GPU), la ventana de vista previa y los proyectores a pantalla completa, uno por
// monitor. Solo se compone y se decodifica mientras hay alguna ventana abierta.
class VideoStage : private juce::Timer
{
public:
    VideoStage (std::array<DeckVideo*, 2> videos, std::array<const Deck*, 2> decks, const Mixer& mixer,
                Compositor::Settings settings);
    ~VideoStage() override;

    std::function<juce::String (int deckIndex)> getTitle;   // titulo de la cancion de un deck
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

    Compositor& getCompositor() { return compositor; }
    void showMenu();   // menu de proyectores (clic derecho en la vista previa)

private:
    class PreviewWindow;
    class FullScreenProjector;

    void timerCallback() override;
    void updateRunning();
    void closeProjectorAt (juce::Rectangle<int> area);
    juce::String statusText() const;

    Compositor compositor;
    VideoMix mix;
    std::array<DeckVideo*, 2> videos;
    std::unique_ptr<PreviewWindow> preview;
    std::vector<std::unique_ptr<FullScreenProjector>> projectors;
};
}
