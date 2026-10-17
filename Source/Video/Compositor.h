#pragma once

#include <juce_graphics/juce_graphics.h>

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
// del compositor en cada fotograma y rellena una lista de capas (de abajo arriba).
class Compositor
{
public:
    // Una capa: una imagen colocada en el programa con una opacidad.
    struct Layer
    {
        int slot = 0;                  // identifica la textura en la GPU (se reutiliza entre fotogramas)
        juce::Image image;             // ARGB premultiplicada de JUCE (B, G, R, A en memoria)
        juce::uint64 version = 0;      // cambia cuando cambia la imagen: solo entonces se sube a la GPU
        juce::Rectangle<float> area;   // en pixeles del programa; vacia = encajada sin deformar en todo el programa
        float opacity = 1.0f;
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

    explicit Compositor (Settings settings);
    ~Compositor();

    bool isReady() const;              // false si no se pudo crear Direct3D 11
    juce::String getError() const;
    int getWidth() const { return settings.width; }
    int getHeight() const { return settings.height; }
    int getFps() const { return settings.fps; }

    // Se llama en el hilo del compositor: las capas del fotograma (la lista llega vacia).
    // Asignarlo antes de start() o con el hilo parado.
    std::function<void (std::vector<Layer>& layers)> buildFrame;

    void start();
    void stop();
    bool isRunning() const;

    // Una salida: una ventana de Windows (HWND) donde se dibuja el programa encajado con bandas
    // negras. Cualquier hilo; la ventana debe vivir hasta removeOutput.
    void addOutput (void* hwnd);
    void removeOutput (void* hwnd);
    int getNumOutputs() const;

    // Sin hilo: compone un fotograma con estas capas y lo presenta en las salidas (pruebas).
    void renderOnce (const std::vector<Layer>& layers);
    // El ultimo fotograma compuesto, leido de la GPU (lento: para pruebas y miniaturas).
    juce::Image readProgram();

    Stats getStats() const;

    // Donde queda una imagen de w x h encajada sin deformar en un area (bandas negras).
    static juce::Rectangle<float> fitInside (float w, float h, juce::Rectangle<float> area);

private:
    struct Impl;
    Settings settings;
    std::unique_ptr<Impl> impl;
};
}
