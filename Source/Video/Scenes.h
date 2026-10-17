#pragma once

#include "Video/Compositor.h"


namespace midj
{
// Escenas como las de OBS: cada escena es una lista de elementos (de abajo arriba) y cada
// elemento muestra una fuente con su posicion, escala, giro, recorte y opacidad.
enum class SourceType
{
    decks,    // la mezcla de los dos decks segun el crossfader (con rotulos y logo)
    deckA,    // solo el deck A
    deckB,    // solo el deck B
    image,    // un archivo PNG/JPG/GIF
    text,     // texto; {titulo}, {artista} y {bpm} se cambian por la cancion que suena
    colour    // un rectangulo de color
};

struct SceneItem
{
    int id = 0;
    SourceType type = SourceType::decks;
    juce::String name;
    bool visible = true;

    // Colocacion (como un elemento de escena de OBS): la esquina de arriba a la izquierda del
    // recuadro sin girar, la escala sobre el tamano de la fuente ya recortada y el giro en
    // grados alrededor del centro del recuadro. El recorte va en pixeles de la fuente.
    juce::Point<float> position;
    float scaleX = 1.0f, scaleY = 1.0f;
    float rotation = 0.0f;
    float opacity = 1.0f;
    float cropLeft = 0.0f, cropTop = 0.0f, cropRight = 0.0f, cropBottom = 0.0f;

    juce::File file;                                 // imagen
    juce::String text;                               // texto
    float fontSize = 72.0f;
    bool bold = true;
    bool shadow = true;
    juce::Colour colour { juce::Colours::white };    // del texto o del rectangulo
    int width = 1280, height = 720;                  // del rectangulo de color
};

struct Scene
{
    int id = 0;
    juce::String name;
    std::vector<SceneItem> items;   // el primero es el de abajo
};

enum class TransitionType { cut, fade, fadeToBlack, slide, swipe };

struct Transition
{
    TransitionType type = TransitionType::fade;
    int milliseconds = 600;
};

// La cancion que suena (la del deck que mas se ve), para los textos con {titulo} etc.
struct NowPlaying
{
    juce::String title, artist;
    double bpm = 0.0;
    bool operator== (const NowPlaying&) const = default;
};

// Todas las escenas, la que esta al aire y la transicion. Se edita en el hilo de interfaz y
// se guarda en XML (%APPDATA%\MiDJ\scenes.xml).
class SceneCollection
{
public:
    std::vector<Scene> scenes;
    int currentScene = 0;   // id
    Transition transition;

    // Una escena "Mezcla" con la mezcla de los decks a pantalla completa (lo de siempre).
    static SceneCollection createDefault (int programWidth, int programHeight);
    std::unique_ptr<juce::XmlElement> toXml() const;
    static SceneCollection fromXml (const juce::XmlElement& xml, int programWidth, int programHeight);

    Scene* findScene (int id);
    const Scene* findScene (int id) const;
    SceneItem* findItem (int itemId);
    int newId();

    Scene& addScene (const juce::String& name);
    // Un elemento nuevo con su tamano natural, centrado (la mezcla y los decks, a pantalla completa).
    SceneItem makeItem (SourceType type, int programWidth, int programHeight);

    static juce::String typeName (SourceType type);
    static juce::String transitionName (TransitionType type);

private:
    int lastId = 0;
};

// Geometria y dibujo de los elementos (comun al motor y a la vista previa).
namespace SceneGeometry
{
    // Recuadro sin girar del elemento, para una fuente de tamano natural w x h.
    juce::Rectangle<float> box (const SceneItem& item, juce::Point<int> natural);
    // Sus cuatro esquinas ya giradas (para la seleccion y para saber donde se hace clic).
    Compositor::Quad quad (const SceneItem& item, juce::Point<int> natural);
    bool contains (const SceneItem& item, juce::Point<int> natural, juce::Point<float> p);

    // La capa de una imagen de pw x ph encajada en el lienzo natural del elemento y recortada
    // (si la imagen tiene el tamano natural, la llena). false si no queda nada visible.
    bool place (Compositor::Layer& layer, const SceneItem& item, juce::Point<int> natural, float pw, float ph);

    // Texto de un elemento con {titulo}, {artista} y {bpm} sustituidos.
    juce::String resolveText (const juce::String& text, const NowPlaying& now);
    // Imagen del texto (ARGB, con margen para la sombra).
    juce::Image renderText (const SceneItem& item, const juce::String& text);
}
}
