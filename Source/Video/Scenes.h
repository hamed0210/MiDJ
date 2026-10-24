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
    text,     // texto; {titulo}, {artista} y {bpm} se cambian por la cancion que suena ({yt_titulo}, {yt_canal}: YouTube)
    colour,   // un rectangulo de color
    screen,   // captura de una pantalla (monitor)
    window,   // captura de una ventana
    visualizer,   // espectro, onda o pulso de lo que suena por el master
    media,        // un archivo de video o audio (la fuente Multimedia de OBS)
    captureDevice, // capturadora de video o webcam (Dispositivo de captura de video de OBS)
    youtube       // el reproductor de YouTube de MiDJ (la captura de su ventana)
};

// Como dibuja el visualizador lo que suena.
enum class VisualizerStyle
{
    spectrum,   // barras por frecuencias (de graves a agudos)
    wave,       // la forma de onda (osciloscopio)
    pulse       // un circulo que late con el ritmo y crece con el volumen
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
    juce::Colour colour { juce::Colours::white };    // del texto, del rectangulo o del visualizador
    int width = 1280, height = 720;                  // del rectangulo de color o del visualizador

    int monitor = 0;                                 // captura de pantalla: numero y nombre de Windows (p. ej. DISPLAY1)
    juce::String monitorId;
    juce::String windowTitle, windowClass, windowExe;   // captura de ventana: cual (como la guarda OBS)
    bool cursor = true;                              // capturar el puntero del raton
    bool clientArea = true;                          // ventana: solo el contenido, sin bordes ni barra de titulo

    VisualizerStyle visualizer = VisualizerStyle::spectrum;   // visualizador: estilo (usa el color y el tamano)

    bool locked = false;                             // bloqueado: no se mueve en la vista previa (candado de OBS)

    // Multimedia (el archivo va en 'file'). Como en OBS: reiniciar cuando la fuente sale al aire
    // (y pararla cuando deja de estarlo), no mostrar nada al terminar, cerrar el archivo cuando
    // no esta al aire y velocidad del 1 al 200 %.
    bool loop = false, restartOnActivate = true, clearOnEnd = true, closeWhenInactive = false;
    int speedPercent = 100;
    // Video con mate (como el "Track Matte" de OBS): el color en la mitad izquierda y la
    // transparencia en grises en la derecha (blanco = se ve). La fuente mide la mitad de ancho.
    bool matte = false;
    bool hardwareDecode = true;                      // multimedia y capturadora: decodificar en la tarjeta grafica (OBS: no; aqui si, la CPU es lenta)

    // Sonido de la fuente (multimedia y capturadora): va al master.
    float volume = 1.0f;
    bool muted = false;

    // Dispositivo de captura de video: cual (enlace simbolico de Windows y nombre), formato
    // (sin customFormat, el predeterminado del dispositivo) y su entrada de audio.
    juce::String deviceId, deviceName;
    bool customFormat = false;
    int captureWidth = 0, captureHeight = 0;
    double captureFps = 0.0;
    juce::String captureFormat;
    bool flipVertical = false, deactivateWhenHidden = false, deviceActive = true;
    juce::String audioDevice;                        // "" = sin audio

    // Vuelve los ajustes propios de la fuente a los de por defecto (no el archivo ni el dispositivo,
    // ni la colocacion): el boton "Por defecto" de las propiedades.
    void resetSourceSettings();
};

// Cambio automatico de escena: la ventana pasa a esta escena cuando lo que mas se oye de los
// decks pasa a ser una cancion de solo audio (audio) o un video (video), cuando no suena
// ningun deck (idle) o cuando suena el reproductor de YouTube (youtube, que manda sobre los decks).
// Una escena puede tener varios a la vez (son bits); cada uno, en una sola escena.
enum class SceneAuto { none = 0, audio = 1, video = 2, idle = 4, youtube = 8 };
inline constexpr std::array<SceneAuto, 4> allSceneAutos { SceneAuto::audio, SceneAuto::video, SceneAuto::idle, SceneAuto::youtube };

// La ventana del reproductor de YouTube de MiDJ (la fuente YouTube la captura).
namespace YouTubeWindow
{
    inline const char* const title = "MiDJ YouTube";
    inline const char* const className = "MiDJYouTubePlayer";
}

struct Scene
{
    int id = 0;
    juce::String name;
    std::vector<SceneItem> items;   // el primero es el de abajo
    int autoWhen = 0;               // SceneAuto combinados (0 = nunca)
    bool switchesOn (SceneAuto when) const { return (autoWhen & (int) when) != 0; }
};

enum class TransitionType { cut, fade, fadeToBlack, slide, swipe };

// Transicion propia (la crea el usuario):
//  - stinger: un video que tapa el cambio (suena por el master); la escena cambia debajo en el
//    punto de cambio. Media Foundation no da transparencia: el video se ve entero.
//  - luma: la escena nueva entra por donde una imagen en grises es mas oscura (barrido con forma);
//    sin imagen, un barrido en diagonal.
//  - colour: funde al color y del color a la escena nueva.
enum class CustomTransitionKind { stinger, luma, colour };

struct CustomTransition
{
    int id = 0;
    juce::String name;
    CustomTransitionKind kind = CustomTransitionKind::stinger;
    juce::File file;                  // stinger: el video; luma: la imagen en grises
    int milliseconds = 1200;          // duracion total
    int cutMilliseconds = 600;        // stinger: cuando cambia la escena debajo
    bool matte = false;               // stinger: video con mate (color a la izquierda, transparencia a la derecha)
    float softness = 0.2f;            // luma: suavidad del borde (0..1)
    juce::Colour colour { juce::Colours::black };   // a color
};

struct Transition
{
    TransitionType type = TransitionType::fade;
    int milliseconds = 600;
    int custom = 0;                   // id de una transicion propia (0 = la de 'type')
};

// La cancion que suena (la del deck que mas se ve; o la de YouTube si suena YouTube y ningun
// deck), para los textos con {titulo} etc. Y el video de YouTube abierto ({yt_titulo}, {yt_canal}).
struct NowPlaying
{
    juce::String title, artist;
    double bpm = 0.0;
    juce::String youtubeTitle, youtubeChannel;
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
    std::vector<CustomTransition> customTransitions;
    const CustomTransition* findCustom (int id) const;
    // La propia elegida ahora (nullptr si es una de las de siempre).
    const CustomTransition* currentCustom() const { return transition.custom != 0 ? findCustom (transition.custom) : nullptr; }
    static juce::String customKindName (CustomTransitionKind kind);

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
    static juce::String autoName (SceneAuto when);   // "Cuando suena una cancion de audio"...
    // Da a una escena sus cambios automaticos y se los quita a las demas que los tuvieran.
    void setAuto (int sceneId, int when);
    static juce::String transitionName (TransitionType type);
    static juce::String visualizerName (VisualizerStyle style);

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

    // Texto de un elemento con {titulo}, {artista}, {bpm}, {yt_titulo} y {yt_canal} sustituidos.
    juce::String resolveText (const juce::String& text, const NowPlaying& now);
    // Imagen del texto (ARGB, con margen para la sombra) a 'density' pixeles por pixel del lienzo:
    // el texto se dibuja a la escala a la que se ve, asi no se pixela al agrandarlo ni se dentea
    // al achicarlo. Su lienzo (tamano natural) es la imagen entre density.
    juce::Image renderText (const SceneItem& item, const juce::String& text, float density = 1.0f);
    // La densidad para un texto: su escala (en pasos de 0,25 para no rehacerlo a cada movimiento
    // del raton), de 0,25 a 4 y sin pasar de unos 1000 px de letra.
    float textDensity (const SceneItem& item);
}
}
