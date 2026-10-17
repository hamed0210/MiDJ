#pragma once

#include "Video/Capture.h"
#include "Video/MediaSources.h"
#include "Video/Scenes.h"
#include "Video/VideoMix.h"
#include "Video/Visualizer.h"

#include <map>
#include <mutex>
#include <set>

namespace midj
{
// El programa de video: la escena al aire con sus elementos y las transiciones entre escenas
// (como OBS sin modo estudio: al elegir otra escena se pasa a ella con la transicion).
// El hilo de interfaz publica las escenas con setScenes(); el compositor pide cada fotograma
// con buildFrame(). Las imagenes y los textos se preparan en el hilo de interfaz.
class VideoProgram
{
public:
    VideoProgram (VideoMix& mix, int programWidth, int programHeight);
    // Las capturas de pantalla y ventana (sin gestor, esos elementos no se ven). Antes de setScenes.
    void setCaptures (CaptureManager* manager) { captures = manager; }
    // Hilo de interfaz: la captura de un elemento (para mostrar si va bien), o nullptr.
    std::shared_ptr<Capture> captureFor (int itemId) const { return captures != nullptr ? captures->find (itemId) : nullptr; }
    // Lo que suena por el master, para los visualizadores (sin ella, no se ven). Antes de empezar.
    void setMasterTap (const MasterTap* t) { tap = t; }
    // Las fuentes multimedia y capturadoras (sin gestor, no se ven). Antes de setScenes.
    void setMediaSources (MediaSourceManager* manager) { media = manager; }
    std::shared_ptr<FrameSource> frameSourceFor (int itemId) const { return media != nullptr ? media->find (itemId) : nullptr; }
    // Hilo de interfaz: los elementos que se ven en la escena al aire (las fuentes "activas" de OBS).
    std::set<int> liveItems() const;

    // Hilo de interfaz: las escenas (copia). Solo se vuelven a preparar las imagenes y textos que cambian.
    void setScenes (const SceneCollection& scenes);
    // Hilo de interfaz: la cancion que suena (para los textos con {titulo}...).
    void setNowPlaying (const NowPlaying& now);
    // Hilo de interfaz: pasar a otra escena con la transicion de la coleccion. Durante una
    // transicion, otra la termina de golpe y empieza la nueva.
    void switchTo (int sceneId);
    bool isInTransition() const;

    // Tamano natural de la fuente de un elemento (lo que mide sin escalar ni recortar).
    juce::Point<int> naturalSize (const SceneItem& item) const;

    // Hilo del compositor.
    void buildFrame (Compositor::Frame& frame);

    // Progreso de una transicion (0..1) en el tiempo, como OBS: lineal; deslizar y barrido, suavizado.
    static float easeInOut (float t);
    // Las capas del programa que mezclan las dos escenas (texturas 0 = saliente, 1 = entrante).
    static void transitionLayers (TransitionType type, float t, float width, float height, std::vector<Compositor::Layer>& layers);

private:
    struct Prepared   // un elemento listo para el compositor
    {
        SceneItem item;
        juce::Image image;          // imagen, texto o color
        juce::uint64 version = 0;
        juce::Point<int> natural;
        std::shared_ptr<Capture> capture;   // captura de pantalla o ventana
        std::shared_ptr<FrameSource> frames;   // multimedia o capturadora
    };
    struct PreparedScene
    {
        int id = 0;
        std::vector<Prepared> items;
    };
    struct Cached   // lo que se preparo para cada elemento, para no repetirlo
    {
        juce::String key;
        juce::Image image;
        juce::uint64 version = 0;
    };

    Prepared prepare (const SceneItem& item);
    void republish();
    void sceneLayers (const PreparedScene& scene, std::vector<Compositor::Layer>& layers);

    VideoMix& mix;
    const int width, height;
    CaptureManager* captures = nullptr;
    const MasterTap* tap = nullptr;
    MediaSourceManager* media = nullptr;

    // Hilo de interfaz
    SceneCollection collection;
    NowPlaying now;
    std::map<int, Cached> cache;
    juce::uint64 nextVersion = 1;

    // Compartido con el compositor
    mutable std::mutex lock;
    std::shared_ptr<const std::vector<PreparedScene>> published;
    int current = 0, previous = 0;
    double transitionStart = 0.0;
    Transition transition;
    bool transitioning = false;

    // Hilo del compositor: el estado de cada visualizador (sus barras bajan poco a poco...).
    std::map<int, std::unique_ptr<Visualizer>> visualizers;
    std::set<int> visualizersSeen;
    juce::uint64 visualizerFrame = 0;
};
}
