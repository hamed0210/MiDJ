#pragma once

#include "Video/Compositor.h"
#include "Video/Scenes.h"

#include <map>
#include <mutex>
#include <set>

namespace midj
{
// Captura de pantalla y de ventana, como las de OBS, sin salir de la tarjeta grafica del
// compositor (la imagen capturada se copia de textura a textura, no pasa por la memoria normal):
//  - pantalla: duplicacion del escritorio de DXGI (sin el borde amarillo de Windows 10) con el
//    puntero del raton dibujado encima; si la pantalla es de otra tarjeta, Windows Graphics Capture.
//  - ventana: Windows Graphics Capture (Windows 10 1903 o posterior), con o sin bordes.
namespace ScreenCapture
{
    struct Monitor
    {
        juce::String id;                 // nombre de Windows, p. ej. \\.\DISPLAY1
        juce::Rectangle<int> bounds;     // en pixeles del escritorio
        bool primary = false;
    };
    struct Window
    {
        void* hwnd = nullptr;
        juce::String title, className, exe;
    };

    std::vector<Monitor> monitors();
    // Ventanas que se pueden capturar (visibles, con titulo, no herramientas), por orden de Windows.
    std::vector<Window> windows();
    bool windowCaptureSupported();
    juce::String describe (const Monitor& monitor, int index);   // "Pantalla 1: 1920x1080 (principal)"
    juce::String describe (const Window& window);                // "[chrome.exe] Titulo"

    // La pantalla de un elemento: por su nombre y, si ya no esta, por su numero.
    int findMonitor (const std::vector<Monitor>& list, const juce::String& id, int index);
    // La ventana guardada de un elemento, como OBS: el mismo titulo, clase y programa; si no, el
    // mismo titulo y programa; si no, la misma clase y programa (el titulo cambia, p. ej. en un
    // navegador); si no, el mismo programa.
    void* findWindow (const juce::String& title, const juce::String& className, const juce::String& exe);
}

// Una captura de pantalla o ventana. Si se pierde (la ventana se cierra, cambia la resolucion...)
// el gestor la vuelve a crear por dentro: el objeto es el mismo para el compositor.
class Capture : public Compositor::GpuSource
{
public:
    struct Backend;

    Capture (void* device, const SceneItem& item);
    ~Capture() override;

    Picture update (void* device, void* context) override;
    juce::Point<int> getSize() const override;

    juce::String getKey() const { return key; }
    // "" si captura bien; si no, por que no (para el panel).
    juce::String getStatus() const;
    // Como captura: "DXGI" (duplicacion del escritorio) o "WGC" (Windows Graphics Capture).
    juce::String getMethod() const;
    // Hilo de interfaz: si se perdio, intenta crearla otra vez. true si ahora captura.
    bool reconnect();

    static juce::String keyFor (const SceneItem& item);

private:
    void* device;
    SceneItem item;
    juce::String key;
    mutable std::mutex lock;
    std::unique_ptr<Backend> backend;
    std::vector<std::unique_ptr<Backend>> retired;   // sustituidas, aun en uso en el fotograma en curso
    juce::String status;
    std::atomic<int> width { 0 }, height { 0 };
    int dxgiFailures = 0;
    bool retryDxgi = false;          // pantalla con WGC tras un fallo de DXGI: se reintenta DXGI
    juce::uint32 lastDxgiTry = 0;
};

// Las capturas de los elementos de las escenas (hilo de interfaz).
class CaptureManager
{
public:
    explicit CaptureManager (void* d3d11Device);

    // La captura de un elemento de pantalla o ventana; la misma mientras capture lo mismo.
    std::shared_ptr<Capture> get (const SceneItem& item);
    std::shared_ptr<Capture> find (int itemId) const;
    // Suelta las capturas de los elementos que ya no estan.
    void keepOnly (const std::set<int>& itemIds);
    // De vez en cuando (cada segundo como mucho): vuelve a crear las capturas perdidas.
    void tick();

private:
    void* device;
    std::map<int, std::shared_ptr<Capture>> captures;
    juce::uint32 lastTick = 0;
};
}
