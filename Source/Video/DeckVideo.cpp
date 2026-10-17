#include "Video/DeckVideo.h"

#include <algorithm>

namespace midj
{
DeckVideo::DeckVideo (const Deck& d, int width, bool gpu)
    : juce::Thread ("MiDJ video"), deck (d), maxWidth (width), allowGpu (gpu)
{
    startThread (juce::Thread::Priority::normal);
}

DeckVideo::~DeckVideo()
{
    signalThreadShouldExit();
    notify();
    stopThread (5000);
}

bool DeckVideo::isVideoFile (const juce::File& file)
{
    return file.hasFileExtension ("mp4;mov;m4v;wmv;3gp");
}

void DeckVideo::setFile (const juce::File& file)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (file == wanted)
            return;
        wanted = file;
    }
    notify();
}

void DeckVideo::setActive (bool shouldDecode)
{
    if (active.exchange (shouldDecode) != shouldDecode)
        notify();
}

juce::Image DeckVideo::getFrame() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return latest;
}

void DeckVideo::run()
{
    MediaFoundation::initialiseThisThread();
    std::unique_ptr<VideoDecoder> decoder;
    juce::File current;
    // Tres imagenes que se turnan: una es la entregada, otra la puede estar subiendo el compositor
    // a la GPU y en la libre (nadie mas la tiene) se decodifica. Asi no se copia cada fotograma.
    std::array<juce::Image, 3> pool;
    double delivered = -1.0;

    while (! threadShouldExit())
    {
        if (! active.load())
        {
            wait (-1);   // nadie mira: no se decodifica hasta que se abra la ventana de video
            continue;
        }

        juce::File file;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            file = wanted;
        }
        if (file != current)
        {
            current = file;
            decoder.reset();
            pool = {};
            delivered = -1.0;
            juce::String error;
            if (file != juce::File() && isVideoFile (file))
                decoder = VideoDecoder::open (file, maxWidth, error, allowGpu);   // si falla: como si fuera solo audio
            juce::Image old;
            {
                const juce::SpinLock::ScopedLockType sl (lock);
                std::swap (latest, old);
            }
            videoOpen = decoder != nullptr;
            usingGpu = decoder != nullptr && decoder->isUsingGpu();
            frameTime = -1.0;
            ++frameNumber;
        }

        if (decoder == nullptr)
        {
            wait (-1);   // hasta que cambie la cancion
            continue;
        }

        // La posicion que se oye: el video va con el audio (tambien con pitch, saltos y bucles).
        const double position = juce::jlimit (0.0, juce::jmax (0.0, decoder->getDuration() - 0.001), deck.getPositionSeconds());
        // Como mucho 6 fotogramas por vuelta: tras un salto se ve el video avanzar hasta alcanzar al audio.
        auto free = std::find_if (pool.begin(), pool.end(), [] (const juce::Image& i) { return ! i.isValid() || i.getReferenceCount() == 1; });
        if (free == pool.end())
            *(free = pool.end() - 1) = {};   // las tres en uso (no deberia pasar): una nueva
        auto& target = *free;
        if (decoder->frameAt (position, target, 6) && decoder->getCurrentFrameTime() != delivered)
        {
            delivered = decoder->getCurrentFrameTime();
            auto published = target;
            {
                const juce::SpinLock::ScopedLockType sl (lock);
                std::swap (latest, published);
            }
            frameTime = delivered;   // la imagen vieja se libera fuera del cerrojo
            ++frameNumber;
        }
        // Se mira la posicion cada pocos milisegundos (mas a menudo que los fotogramas de cualquier video).
        wait (deck.isPlaying() ? 8 : 25);
    }
}
}
