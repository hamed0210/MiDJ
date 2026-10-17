#pragma once

#include "Engine/Deck.h"
#include "Video/MediaFoundation.h"

#include <juce_events/juce_events.h>

namespace midj
{
// El video de un deck: un hilo que sigue la posicion del deck y deja listo el fotograma que
// toca (como el motor de video de VirtualDJ, que decodifica aparte del audio). El hilo de
// interfaz le dice que archivo tiene el deck con setFile(); si no es un video, no hay fotograma.
// La interfaz pide el ultimo fotograma con getFrame() cuando pinta.
class DeckVideo : private juce::Thread
{
public:
    // allowGpu: decodificar en la tarjeta grafica si se puede (ver VideoDecoder::open).
    explicit DeckVideo (const Deck& deck, int maxWidth = 960, bool allowGpu = true);
    ~DeckVideo() override;

    // Hilo de interfaz: la cancion que hay en el deck (se abre en el hilo de video).
    void setFile (const juce::File& file);
    // Solo se decodifica mientras alguien mira (ventana de video abierta): si no, el hilo duerme.
    void setActive (bool shouldDecode);
    bool isActive() const { return active.load(); }
    bool isUsingGpu() const { return usingGpu.load(); }

    // Cualquier hilo: el ultimo fotograma (no valido si la cancion no tiene video).
    juce::Image getFrame() const;
    bool hasVideo() const { return videoOpen.load(); }
    double getFrameTime() const { return frameTime.load(); }   // instante del fotograma entregado

    static bool isVideoFile (const juce::File& file);

private:
    void run() override;

    const Deck& deck;
    const int maxWidth;
    const bool allowGpu;
    std::atomic<bool> active { false }, usingGpu { false };
    mutable juce::SpinLock lock;
    juce::Image latest;
    juce::File wanted;
    std::atomic<bool> videoOpen { false };
    std::atomic<double> frameTime { -1.0 };
};
}
