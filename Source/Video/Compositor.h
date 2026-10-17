#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace midj
{
// Compositor de video en la tarjeta grafica (Direct3D 11), como el nucleo de OBS: un hilo a
// ritmo fijo compone cada fotograma una sola vez en una textura del tamano del programa
// (1280x720 por defecto) y luego lo dibuja en cada salida (ventana de vista previa, proyectores
// a pantalla completa). Tener varias salidas apenas cuesta: no se vuelve a componer.
//
// Lo que se dibuja lo dice quien use el compositor con 'buildFrame', que se llama en el hilo
// del compositor en cada fotograma y rellena las capas (de abajo arriba). Para las transiciones
// entre escenas, cada escena se compone antes en su propia textura y el programa las mezcla
// como dos capas mas (fundido, deslizar...).
class Compositor
{
public:
    static constexpr int imageSource = -1;   // la capa es una imagen; 0 o 1: la textura de esa escena

    // Una fuente que ya esta en la tarjeta grafica (captura de pantalla o de ventana): el compositor
    // le pide su textura en cada fotograma y la dibuja sin pasar por la memoria normal.
    class GpuSource
    {
    public:
        struct Picture
        {
            void* view = nullptr;            // ID3D11ShaderResourceView* BGRA; nullptr = aun no hay imagen
            int width = 0, height = 0;
            bool opaque = true;              // sin alfa (el escritorio): se dibuja opaca
            void* cursorView = nullptr;      // el puntero del raton encima (BGRA premultiplicado), o nullptr
            juce::Rectangle<float> cursor;   // donde, en pixeles de la imagen
        };
        virtual ~GpuSource() = default;
        // Hilo del compositor, con su contexto de Direct3D (ID3D11Device*, ID3D11DeviceContext*):
        // pone al dia la imagen. Se llama una vez por fotograma aunque salga en varias capas.
        virtual Picture update (void* device, void* context) = 0;
        // Tamano de la ultima imagen (cualquier hilo); 0 x 0 si aun no hay.
        virtual juce::Point<int> getSize() const = 0;
    };

    // Una capa: una imagen (o una escena ya compuesta) colocada en el programa.
    struct Layer
    {
        int slot = 0;                  // identifica la textura en la GPU (se reutiliza entre fotogramas)
        juce::Image image;             // ARGB premultiplicada de JUCE (B, G, R, A en memoria)
        juce::uint64 version = 0;      // cambia cuando cambia la imagen: solo entonces se sube a la GPU
        int source = imageSource;
        std::shared_ptr<GpuSource> gpu;   // si hay, la imagen es la de esta fuente (no se usa 'image')
        juce::Rectangle<float> area;   // en pixeles del programa; vacia = encajada sin deformar en todo el programa
        juce::Rectangle<float> uv { 0.0f, 0.0f, 1.0f, 1.0f };   // parte de la imagen que se ve (recorte)
        float rotation = 0.0f;         // grados en el sentido del reloj, alrededor del centro del area
        juce::AffineTransform transform;   // se aplica despues (p. ej. el giro de un elemento de escena)
        float opacity = 1.0f;
    };

    // Lo que se dibuja en un fotograma.
    struct Frame
    {
        std::array<std::vector<Layer>, 2> scenes;   // se componen antes en las texturas de escena 0 y 1
        std::array<bool, 2> useScene {};            // (solo las que se usan en una transicion)
        std::vector<Layer> layers;                  // el programa

        void clear()
        {
            for (auto& s : scenes) s.clear();
            useScene = {};
            layers.clear();
        }
    };

    struct Settings
    {
        int width = 1280, height = 720;
        int fps = 30;
        bool allowHardware = true;     // false: Direct3D por software (WARP), p. ej. para pruebas
    };

    struct Stats
    {
        juce::uint64 frames = 0;       // fotogramas compuestos
        juce::uint64 lagged = 0;       // fotogramas que no dio tiempo a hacer (el hilo llego tarde)
        double renderMs = 0.0;         // media reciente de lo que tarda componer y presentar
        bool hardware = false;         // en la tarjeta grafica (si no, WARP)
        juce::String adapter;          // nombre de la tarjeta grafica
    };

    // Un cuadrilatero en pixeles del programa: arriba izquierda, arriba derecha, abajo derecha, abajo izquierda.
    using Quad = std::array<juce::Point<float>, 4>;

    explicit Compositor (Settings settings);
    ~Compositor();

    bool isReady() const;              // false si no se pudo crear Direct3D 11
    // El ID3D11Device* (para crear las capturas en la misma tarjeta).
    void* getDevice() const;
    juce::String getError() const;
    int getWidth() const { return settings.width; }
    int getHeight() const { return settings.height; }
    int getFps() const { return settings.fps; }

    // Se llama en el hilo del compositor: lo que se dibuja (el fotograma llega vacio).
    // Asignarlo antes de start() o con el hilo parado.
    std::function<void (Frame& frame)> buildFrame;

    void start();
    void stop();
    bool isRunning() const;

    // Una salida: una ventana de Windows (HWND) donde se dibuja el programa encajado con bandas
    // negras. Cualquier hilo; la ventana debe vivir hasta removeOutput.
    void addOutput (void* hwnd);
    void removeOutput (void* hwnd);
    int getNumOutputs() const;
    // Recuadros que se dibujan encima del programa solo en esa salida (la seleccion de la vista previa).
    void setOverlay (void* hwnd, std::vector<Quad> boxes);

    // Sin hilo: compone un fotograma y lo presenta en las salidas (pruebas).
    void renderOnce (const std::vector<Layer>& layers);
    void renderOnce (const Frame& frame);
    // El ultimo fotograma compuesto, leido de la GPU (lento: para pruebas y miniaturas).
    juce::Image readProgram();

    Stats getStats() const;

    // Donde queda una imagen de w x h encajada sin deformar en un area (bandas negras).
    static juce::Rectangle<float> fitInside (float w, float h, juce::Rectangle<float> area);
    // Las cuatro esquinas de un area girada alrededor de su centro.
    static Quad corners (juce::Rectangle<float> area, float rotationDegrees);

private:
    struct Impl;
    Settings settings;
    std::unique_ptr<Impl> impl;
};
}
