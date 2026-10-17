#pragma once

#include "Video/Scenes.h"
#include "Video/VideoMix.h"

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
};
}
