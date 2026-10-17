#pragma once

#include "Video/Compositor.h"
#include "Video/MediaFoundation.h"

#include <juce_core/juce_core.h>

namespace midj
{
// Fotogramas de video que no salen de la tarjeta grafica (como en OBS): un decodificador que
// usa el dispositivo del compositor deja aqui su ultimo fotograma y el compositor, en su hilo,
// lo copia de textura a textura a una suya (una copia en la GPU, nada pasa por la CPU) y lo
// dibuja. El fotograma de Media Foundation se suelta en cuanto se copia.
class GpuFrameSource : public Compositor::GpuSource
{
public:
    // Hilo del decodificador.
    void publish (std::shared_ptr<GpuVideoFrame> frame);
    void clear();   // no se ve nada hasta el siguiente publish

    // Cualquier hilo: hay imagen que mostrar.
    bool hasPicture() const { return showing.load(); }
    juce::uint64 getFrameNumber() const { return frameNumber.load(); }

    // Compositor::GpuSource (hilo del compositor).
    Picture update (void* device, void* context) override;
    juce::Point<int> getSize() const override { return { width.load(), height.load() }; }

private:
    juce::SpinLock lock;
    std::shared_ptr<GpuVideoFrame> pending;
    std::atomic<bool> showing { false };
    std::atomic<int> width { 0 }, height { 0 };
    std::atomic<juce::uint64> frameNumber { 0 };

    // Hilo del compositor
    struct Texture;
    std::shared_ptr<Texture> texture;
};
}
