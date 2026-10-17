#include "Engine/StemsService.h"

namespace midj
{
StemsService::StemsService (juce::AudioFormatManager& f, const juce::File& dataFolder, const juce::File& models)
    : juce::Thread ("MiDJ stems"), formats (f), cacheFolder (dataFolder.getChildFile ("Stems")),
      settingsFile (dataFolder.getChildFile ("stems.xml")), modelFolder (models)
{
    startThread (juce::Thread::Priority::low);
}

StemsService::~StemsService()
{
    *alive = false;
    signalThreadShouldExit();
    wake.signal();
    stopThread (30000);   // la separacion se cancela entre trozos (unos segundos como mucho)
}

juce::File StemsService::cacheFileFor (const juce::File& song) const
{
    // Ruta + tamano + fecha: si el archivo cambia, la voz se vuelve a separar.
    const auto key = song.getFullPathName() + "|" + juce::String (song.getSize()) + "|"
                     + juce::String (song.getLastModificationTime().toMilliseconds());
    return cacheFolder.getChildFile (juce::String::toHexString (key.hashCode64()) + ".flac");
}

bool StemsService::isCached (const juce::File& song) const
{
    return cacheFileFor (song).existsAsFile();
}

void StemsService::setStatus (const juce::File& song, Status s, double progress)
{
    {
        const juce::ScopedLock sl (lock);
        states[song.getFullPathName()] = { s, progress };
    }
    juce::MessageManager::callAsync ([this, flag = alive] { if (flag->load() && onProgress) onProgress(); });
}

StemsService::Status StemsService::getStatus (const juce::File& song) const
{
    const juce::ScopedLock sl (lock);
    const auto it = states.find (song.getFullPathName());
    return it == states.end() ? Status::none : it->second.first;
}

double StemsService::getProgress (const juce::File& song) const
{
    const juce::ScopedLock sl (lock);
    const auto it = states.find (song.getFullPathName());
    return it == states.end() ? 0.0 : it->second.second;
}

juce::String StemsService::getDeviceDescription() const
{
    return separator.isLoaded() ? separator.describe() : juce::String ("modelo sin cargar");
}

void StemsService::request (const juce::File& song, bool urgent)
{
    {
        const juce::ScopedLock sl (lock);
        const auto it = states.find (song.getFullPathName());
        if (it != states.end() && it->second.first == Status::working)
            return;   // ya se esta haciendo (si ya esta hecha, se vuelve a leer de la cache: es rapido)
        queue.erase (std::remove (queue.begin(), queue.end(), song), queue.end());
        if (urgent) queue.push_front (song);
        else        queue.push_back (song);
        states[song.getFullPathName()] = { Status::queued, 0.0 };
    }
    wake.signal();
}

bool StemsService::ensureModel (juce::String& error)
{
    if (separator.isLoaded())
        return true;
    if (modelTried)
    {
        error = "No se pudo cargar el modelo de stems";
        return false;
    }
    modelTried = true;

    // GPU solo si se ha activado a mano en stems.xml (device="gpu").
    auto settings = juce::XmlDocument::parse (settingsFile);
    const bool wantGpu = settings != nullptr && settings->getStringAttribute ("device") == "gpu";
    if (wantGpu && separator.load (modelFolder, StemSeparator::Device::gpu, error))
        return true;
    if (! separator.load (modelFolder, StemSeparator::Device::cpu, error))
        return false;
    if (wantGpu)   // la GPU no cargo: se deja apuntado
    {
        juce::XmlElement xml ("STEMS");
        xml.setAttribute ("device", "cpu");
        xml.setAttribute ("gpuFailed", error);
        xml.writeTo (settingsFile);
    }
    return true;
}

std::shared_ptr<juce::AudioBuffer<float>> StemsService::separate (const juce::File& song, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (song));
    if (reader == nullptr)
    {
        error = "No se puede leer " + song.getFileName();
        return nullptr;
    }
    // Igual que el deck: estereo y a su frecuencia, para que la voz mida lo mismo.
    const double rate = reader->sampleRate;
    const int length = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> audio (2, length);
    reader->read (&audio, 0, length, 0, true, true);
    reader.reset();

    if (! ensureModel (error))
        return nullptr;
    juce::AudioBuffer<float> vocals44;
    {
        const auto at44 = StemSeparator::resample (audio, rate, StemSeparator::modelSampleRate);
        audio.setSize (0, 0);   // libera memoria mientras se separa
        const bool ok = separator.separateVocals (at44, vocals44, [this, &song] (double p)
        {
            setStatus (song, Status::working, p);
            return ! threadShouldExit();
        });
        if (! ok)
        {
            error = threadShouldExit() ? juce::String ("cancelado") : "Fallo la separacion: " + separator.getLastError();
            return nullptr;
        }
    }
    auto vocals = std::make_shared<juce::AudioBuffer<float>> (StemSeparator::resample (vocals44, StemSeparator::modelSampleRate, rate));
    vocals->setSize (2, length, true, true);   // el remuestreo puede dejar una muestra de mas o de menos

    // A la cache como FLAC de 24 bits.
    cacheFolder.createDirectory();
    const auto file = cacheFileFor (song);
    file.deleteFile();
    juce::FlacAudioFormat flac;
    if (std::unique_ptr<juce::AudioFormatWriter> w { flac.createWriterFor (new juce::FileOutputStream (file), rate, 2, 24, {}, 0) })
        w->writeFromAudioSampleBuffer (*vocals, 0, length);
    return vocals;
}

void StemsService::run()
{
    while (! threadShouldExit())
    {
        juce::File song;
        {
            const juce::ScopedLock sl (lock);
            if (! queue.empty())
            {
                song = queue.front();
                queue.pop_front();
            }
        }
        if (song == juce::File())
        {
            wake.wait (-1);
            continue;
        }

        std::shared_ptr<juce::AudioBuffer<float>> vocals;
        juce::String error;
        setStatus (song, Status::working, 0.0);
        const auto cached = cacheFileFor (song);
        if (cached.existsAsFile())
        {
            if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor (cached) })
            {
                vocals = std::make_shared<juce::AudioBuffer<float>> (2, (int) r->lengthInSamples);
                r->read (vocals.get(), 0, (int) r->lengthInSamples, 0, true, true);
            }
        }
        if (vocals == nullptr)
            vocals = separate (song, error);
        if (threadShouldExit())
            break;

        setStatus (song, vocals != nullptr ? Status::ready : Status::failed, vocals != nullptr ? 1.0 : 0.0);
        std::shared_ptr<const juce::AudioBuffer<float>> result = vocals;
        juce::MessageManager::callAsync ([this, song, result, error, flag = alive]
        {
            if (! flag->load())
                return;
            if (result != nullptr && onReady)       onReady (song, result);
            else if (result == nullptr && onFailed) onFailed (song, error);
        });
    }
}
}
