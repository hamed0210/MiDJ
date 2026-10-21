#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>
#include <set>

namespace midj
{
// Miniaturas de los videos de YouTube (las de i.ytimg.com, 320x180), descargadas en segundo plano
// de dos en dos y guardadas en memoria a 160x90 mientras MiDJ esta abierto. La comparten la
// biblioteca y la pestana YOUTUBE (juce::SharedResourcePointer). Avisa con sendChangeMessage()
// cuando llega una. Hilo de la interfaz.
class YouTubeThumbnails : public juce::ChangeBroadcaster
{
public:
    YouTubeThumbnails();
    ~YouTubeThumbnails() override;

    // La miniatura si ya esta; si no, una imagen vacia y se pide (llegara con un aviso).
    juce::Image get (const juce::String& videoId);

    // La imagen recortada a 16:9 dentro de un rectangulo con esquinas redondeadas.
    static void draw (juce::Graphics& g, const juce::Image& image, juce::Rectangle<float> area, float corner = 4.0f);

private:
    juce::ThreadPool pool { 2 };
    std::map<juce::String, juce::Image> images;
    std::set<juce::String> requested;   // pedidas (llegadas o no; las fallidas no se repiten)
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};
}
