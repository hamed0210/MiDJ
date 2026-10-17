#include "Video/DeckVideo.h"

namespace midj
{
DeckVideo::DeckVideo (const Deck& d, int width) : juce::Thread ("MiDJ video"), deck (d), maxWidth (width)
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
    juce::Image decoded;   // solo de este hilo; lo que se entrega es una copia
    double delivered = -1.0;

    while (! threadShouldExit())
    {
        juce::File file;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            file = wanted;
        }
        if (file != current)
        {
            current = file;
            decoder.reset();
            decoded = {};
            delivered = -1.0;
            juce::String error;
            if (file != juce::File() && isVideoFile (file))
                decoder = VideoDecoder::open (file, maxWidth, error);   // si falla: como si fuera solo audio
            juce::Image old;
            {
                const juce::SpinLock::ScopedLockType sl (lock);
                std::swap (latest, old);
            }
            videoOpen = decoder != nullptr;
            frameTime = -1.0;
        }

        if (decoder == nullptr)
        {
            wait (-1);   // hasta que cambie la cancion
            continue;
        }

        // La posicion que se oye: el video va con el audio (tambien con pitch, saltos y bucles).
        const double position = juce::jlimit (0.0, juce::jmax (0.0, decoder->getDuration() - 0.001), deck.getPositionSeconds());
        // Como mucho 6 fotogramas por vuelta: tras un salto se ve el video avanzar hasta alcanzar al audio.
        if (decoder->frameAt (position, decoded, 6) && decoder->getCurrentFrameTime() != delivered)
        {
            delivered = decoder->getCurrentFrameTime();
            auto copy = decoded.createCopy();
            {
                const juce::SpinLock::ScopedLockType sl (lock);
                std::swap (latest, copy);
            }
            frameTime = delivered;   // la imagen vieja se libera fuera del cerrojo
        }
        // Se mira la posicion cada pocos milisegundos (mas a menudo que los fotogramas de cualquier video).
        wait (deck.isPlaying() ? 8 : 25);
    }
}
}
