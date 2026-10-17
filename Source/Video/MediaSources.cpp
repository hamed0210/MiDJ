#include "Video/MediaSources.h"

#include <algorithm>

namespace midj
{
namespace
{
    double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

    // Tres imagenes que se turnan (como en DeckVideo): una entregada, otra quiza subiendose a la
    // GPU y en la libre se escribe el siguiente fotograma, sin copiar.
    juce::Image& freeImage (std::array<juce::Image, 3>& pool)
    {
        auto it = std::find_if (pool.begin(), pool.end(), [] (const juce::Image& i) { return ! i.isValid() || i.getReferenceCount() == 1; });
        if (it == pool.end())
            *(it = pool.end() - 1) = {};
        return *it;
    }
}

// --- Fuente con fotogramas ---------------------------------------------------------------------

juce::Image FrameSource::getFrame() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return latest;
}

void FrameSource::publish (const juce::Image& image)
{
    auto copy = image;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (latest, copy);
    }
    if (image.isValid())
    {
        width = image.getWidth();
        height = image.getHeight();
    }
    ++frameNumber;   // la imagen vieja se suelta fuera del cerrojo
}

// --- Multimedia --------------------------------------------------------------------------------

MediaFileSource::MediaFileSource (const SceneItem& item, AuxBus& b, juce::AudioFormatManager& f,
                                  juce::TimeSliceThread& ra, int mw)
    : juce::Thread ("MiDJ multimedia"), file (item.file), allowGpu (item.hardwareDecode), maxWidth (mw),
      bus (b), formats (f), readAhead (ra)
{
    key = keyFor (item);
    update (item);
    startThread (juce::Thread::Priority::normal);
    bus.add (this);
    // Como OBS: sin "cerrar cuando no este activa" se abre ya, y sin "reiniciar al activarse"
    // empieza a sonar desde que se crea.
    if (! closeWhenInactive)
        openFile();
    if (! restartOnActivate)
        play();
}

MediaFileSource::~MediaFileSource()
{
    bus.remove (this);
    signalThreadShouldExit();
    notify();
    stopThread (5000);
    closeFile();
}

juce::String MediaFileSource::keyFor (const SceneItem& item)
{
    return "media|" + item.file.getFullPathName() + "|" + juce::String ((int) item.hardwareDecode);
}

juce::String MediaFileSource::fileFilter()
{
    return "*.mp4;*.m4v;*.mov;*.wmv;*.avi;*.mkv;*.webm;*.3gp;*.mp3;*.m4a;*.aac;*.wav;*.flac;*.ogg;*.wma";
}

void MediaFileSource::update (const SceneItem& item)
{
    loop = item.loop;
    looping = item.loop;
    if (readerSource != nullptr)
        readerSource->setLooping (loop);
    restartOnActivate = item.restartOnActivate;
    clearOnEnd = item.clearOnEnd;
    closeWhenInactive = item.closeWhenInactive;
    const double newSpeed = juce::jlimit (1, 200, item.speedPercent) / 100.0;
    if (newSpeed != speed.load())
    {
        setClockPosition (getPosition());   // el reloj sigue desde donde iba, a la nueva velocidad
        speed = newSpeed;
        resampler.setResamplingRatio (newSpeed);
    }
    gain = item.muted ? 0.0f : juce::jlimit (0.0f, 2.0f, item.volume);
}

void MediaFileSource::openFile()
{
    if (opened)
        return;
    opened = true;
    ended = false;
    hidden = false;
    if (auto* reader = formats.createReaderFor (file))
    {
        const double rate = reader->sampleRate;
        duration = rate > 0.0 ? (double) reader->lengthInSamples / rate : 0.0;
        readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
        readerSource->setLooping (loop);
        transport.setSource (readerSource.get(), 32768, &readAhead, rate, 2);
        audioOpen = true;
    }
    setClockPosition (0.0);
    wantOpen = true;
    notify();
}

void MediaFileSource::closeFile()
{
    if (! opened)
        return;
    opened = false;
    playing = false;
    wantOpen = false;
    notify();
    {
        const juce::SpinLock::ScopedLockType sl (positionLock);
        audioOpen = false;
    }
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();
    publish ({});
}

double MediaFileSource::getPosition() const
{
    {
        const juce::SpinLock::ScopedLockType sl (positionLock);
        if (audioOpen.load())
            return transport.getCurrentPosition();
    }
    double p = clockBase.load();
    if (playing.load())
        p += (nowMs() - clockStartMs.load()) * 0.001 * speed.load();
    const double d = duration.load();
    if (looping.load() && d > 0.0)
        p = std::fmod (p, d);
    return p;
}

void MediaFileSource::setClockPosition (double seconds)
{
    if (audioOpen.load())
        transport.setPosition (seconds);
    clockBase = seconds;
    clockStartMs = nowMs();
}

void MediaFileSource::play()
{
    if (! file.existsAsFile())
        return;   // sin archivo no hay nada que reproducir
    if (! opened)
        openFile();
    if (ended)
    {
        setClockPosition (0.0);   // al terminar, Play vuelve a empezar (como OBS)
        ended = false;
    }
    hidden = false;
    if (playing.load())
        return;
    clockStartMs = nowMs();
    if (audioOpen.load())
        transport.start();
    playing = true;
    notify();
}

void MediaFileSource::pause()
{
    if (! playing.load())
        return;
    clockBase = getPosition();
    if (audioOpen.load())
        transport.stop();
    playing = false;
}

void MediaFileSource::stop()
{
    pause();
    setClockPosition (0.0);
    ended = false;
    if (clearOnEnd)
        hidden = true;
}

void MediaFileSource::restart()
{
    if (! opened)
        openFile();
    pause();
    setClockPosition (0.0);
    ended = false;
    play();
}

void MediaFileSource::setOnAir (bool on)
{
    if (on == onAir)
        return;
    onAir = on;
    if (on)
    {
        const bool wasClosed = ! opened;
        openFile();
        if (restartOnActivate)
            restart();
        else if (wasClosed)
            play();
    }
    else
    {
        if (restartOnActivate)
        {
            pause();
            setClockPosition (0.0);
            if (clearOnEnd)
                hidden = true;
        }
        if (closeWhenInactive)
            closeFile();
    }
}

void MediaFileSource::setViewing (bool v)
{
    if (viewing.exchange (v) != v)
        notify();
}

void MediaFileSource::tick()
{
    if (! opened || ! playing.load())
        return;
    const double d = duration.load();
    const bool finished = audioOpen.load() ? transport.hasStreamFinished()
                                           : (d > 0.0 && ! loop && getPosition() >= d);
    if (! finished)
        return;
    pause();
    ended = true;
    if (clearOnEnd)
        hidden = true;   // no mostrar nada al terminar
}

juce::String MediaFileSource::getStatus() const
{
    if (! file.existsAsFile())
        return file == juce::File() ? "Elige un archivo con Examinar" : "No se encuentra el archivo";
    if (! opened)
        return "Cerrado mientras no esta al aire";
    if (videoFailed.load() && ! audioOpen.load())
        return "No se pudo abrir: " + videoError;
    return {};
}

void MediaFileSource::prepare (double sampleRate, int blockSize)
{
    resampler.prepareToPlay (blockSize, sampleRate);
    scratch.setSize (2, blockSize, false, true, true);
}

void MediaFileSource::addTo (float* left, float* right, int n)
{
    if (! audioOpen.load() || ! playing.load() || scratch.getNumSamples() == 0)
        return;
    const float g = gain.load();
    for (int done = 0; done < n;)
    {
        const int count = juce::jmin (n - done, scratch.getNumSamples());
        juce::AudioSourceChannelInfo info (&scratch, 0, count);
        resampler.getNextAudioBlock (info);
        juce::FloatVectorOperations::addWithMultiply (left + done, scratch.getReadPointer (0), g, count);
        if (right != nullptr)
            juce::FloatVectorOperations::addWithMultiply (right + done, scratch.getReadPointer (1), g, count);
        done += count;
    }
}

void MediaFileSource::run()
{
    MediaFoundation::initialiseThisThread();
    std::unique_ptr<VideoDecoder> decoder;
    bool tried = false, shown = false;
    std::array<juce::Image, 3> pool;
    double delivered = -1.0;

    while (! threadShouldExit())
    {
        if (! wantOpen.load())
        {
            decoder.reset();
            pool = {};
            tried = false;
            if (shown)
                publish ({});
            shown = false;
            wait (-1);
            continue;
        }
        if (! tried)
        {
            // Se abre aunque nadie mire: asi se sabe cuanto dura un video sin sonido.
            tried = true;
            juce::String error;
            decoder = VideoDecoder::open (file, maxWidth, error, allowGpu);
            videoError = error;
            videoFailed = decoder == nullptr;
            if (decoder != nullptr && ! audioOpen.load())
                duration = decoder->getDuration();
            delivered = -1.0;
        }
        if (decoder == nullptr || ! viewing.load())
        {
            wait (-1);   // sin video, o nadie mira: no se decodifica
            continue;
        }
        if (hidden.load())
        {
            if (shown)
                publish ({});
            shown = false;
            delivered = -1.0;
            wait (30);
            continue;
        }
        const double position = juce::jlimit (0.0, juce::jmax (0.0, decoder->getDuration() - 0.001), getPosition());
        auto& target = freeImage (pool);
        if (decoder->frameAt (position, target, 6) && (decoder->getCurrentFrameTime() != delivered || ! shown))
        {
            delivered = decoder->getCurrentFrameTime();
            publish (target);
            shown = true;
        }
        wait (playing.load() ? 8 : 30);
    }
}

// --- Capturadora -------------------------------------------------------------------------------

CaptureDeviceSource::CaptureDeviceSource (const SceneItem& item, AuxBus& b, int mw)
    : juce::Thread ("MiDJ capturadora"), deviceId (item.deviceId),
      format (item.customFormat ? VideoCaptureReader::Format { item.captureWidth, item.captureHeight, item.captureFps, item.captureFormat }
                                : VideoCaptureReader::Format {}),
      allowGpu (item.hardwareDecode), maxWidth (mw), bus (b)
{
    key = keyFor (item);
    update (item);
    startThread (juce::Thread::Priority::normal);
}

CaptureDeviceSource::~CaptureDeviceSource()
{
    signalThreadShouldExit();
    {
        const std::lock_guard<std::mutex> sl (readerLock);
        if (current != nullptr)
            current->interrupt();
    }
    notify();
    stopThread (5000);
    if (audioRegistered)
        bus.remove (&audio);
    audio.close();
}

juce::String CaptureDeviceSource::keyFor (const SceneItem& item)
{
    juce::String k = "dev|" + item.deviceId + "|" + juce::String ((int) item.hardwareDecode);
    if (item.customFormat)
        k << "|" << item.captureWidth << "x" << item.captureHeight << "@" << item.captureFps << item.captureFormat;
    return k;
}

juce::String CaptureDeviceSource::guessAudioInput (const juce::String& deviceName, const juce::StringArray& inputs)
{
    const auto name = deviceName.toLowerCase().trim();
    if (name.isNotEmpty())
        for (const auto& input : inputs)
            if (input.toLowerCase().contains (name) || name.contains (input.toLowerCase().upToFirstOccurrenceOf ("(", false, false).trim()))
                return input;
    // Las capturadoras USB/HDMI suelen llamar asi a su audio.
    for (const auto& input : inputs)
    {
        const auto l = input.toLowerCase();
        for (auto* word : { "capture", "captura", "hdmi", "cam link", "elgato", "usb3" })
            if (l.contains (word))
                return input;
    }
    return {};
}

bool CaptureDeviceSource::shouldRun() const
{
    return deviceId.isNotEmpty() && active.load() && viewing.load() && (onAir.load() || ! deactivateWhenHidden.load());
}

void CaptureDeviceSource::update (const SceneItem& item)
{
    flip = item.flipVertical;
    active = item.deviceActive;
    deactivateWhenHidden = item.deactivateWhenHidden;
    audio.setGain (item.muted ? 0.0f : juce::jlimit (0.0f, 2.0f, item.volume));
    audioWanted = item.audioDevice;
    updateAudio();
    notify();
}

void CaptureDeviceSource::setOnAir (bool on)
{
    if (onAir.exchange (on) == on)
        return;
    updateAudio();
    notify();
}

void CaptureDeviceSource::setViewing (bool v)
{
    if (viewing.exchange (v) != v)
        notify();
}

void CaptureDeviceSource::updateAudio()
{
    // El sonido sigue aunque nadie mire el video; se apaga con el dispositivo.
    const bool wanted = audioWanted.isNotEmpty() && active.load() && (onAir.load() || ! deactivateWhenHidden.load());
    if (wanted)
    {
        if (! audioRegistered)
        {
            bus.add (&audio);   // antes de abrir: asi sabe la frecuencia de la salida
            audioRegistered = true;
        }
        if (audio.getName() != audioWanted)
        {
            audioError = {};
            if (! audio.open (audioWanted, audioError))
                audioError = "Audio: " + audioError;
        }
    }
    else
    {
        if (audioRegistered)
        {
            bus.remove (&audio);
            audioRegistered = false;
        }
        audio.close();
        audioError = {};
    }
}

juce::String CaptureDeviceSource::getStatus() const
{
    juce::String s;
    {
        const juce::SpinLock::ScopedLockType sl (statusLock);
        s = status;
    }
    if (audioError.isNotEmpty())
        s << (s.isEmpty() ? "" : " / ") << audioError;
    return s;
}

void CaptureDeviceSource::run()
{
    MediaFoundation::initialiseThisThread();
    auto setStatus = [this] (const juce::String& s)
    {
        const juce::SpinLock::ScopedLockType sl (statusLock);
        status = s;
    };
    std::array<juce::Image, 3> pool;
    while (! threadShouldExit())
    {
        if (! shouldRun())
        {
            // Apagada o nadie mira: el dispositivo queda libre (y no gasta CPU).
            setStatus (active.load() ? juce::String() : juce::String ("Desactivado"));
            publish ({});
            pool = {};
            wait (-1);
            continue;
        }
        juce::String error;
        auto reader = VideoCaptureReader::open (deviceId, format, maxWidth, allowGpu, error);
        if (reader == nullptr)
        {
            setStatus (error);
            wait (2000);   // se vuelve a probar (p. ej. al conectarla)
            continue;
        }
        {
            const std::lock_guard<std::mutex> sl (readerLock);
            current = reader.get();
        }
        const auto f = reader->getFormat();
        setStatus ({});
        bool lost = false;
        while (! threadShouldExit() && shouldRun())
        {
            auto& target = freeImage (pool);
            if (! reader->read (target, flip.load()))
            {
                lost = true;
                break;
            }
            publish (target);
        }
        {
            const std::lock_guard<std::mutex> sl (readerLock);
            current = nullptr;
        }
        reader.reset();
        if (lost && ! threadShouldExit())
        {
            setStatus ("Sin senal o desconectada (" + juce::String (f.width) + "x" + juce::String (f.height) + " " + f.format + ")");
            wait (2000);
        }
    }
}

// --- Gestor ------------------------------------------------------------------------------------

MediaSourceManager::MediaSourceManager (AuxBus& b, juce::AudioFormatManager& f, int w)
    : bus (b), formats (f), maxWidth (w)
{
    readAhead.startThread (juce::Thread::Priority::high);
}

MediaSourceManager::~MediaSourceManager()
{
    sources.clear();
    readAhead.stopThread (2000);
}

std::shared_ptr<FrameSource> MediaSourceManager::get (const SceneItem& item)
{
    const auto key = item.type == SourceType::media ? MediaFileSource::keyFor (item) : CaptureDeviceSource::keyFor (item);
    auto& s = sources[item.id];
    if (s == nullptr || s->getKey() != key)
    {
        s.reset();
        for (const auto& [id, other] : sources)
            if (other != nullptr && other->getKey() == key)
                s = other;
        if (s == nullptr)
        {
            if (item.type == SourceType::media)
                s = std::make_shared<MediaFileSource> (item, bus, formats, readAhead, maxWidth);
            else
                s = std::make_shared<CaptureDeviceSource> (item, bus, maxWidth);
            s->setViewing (viewing);
        }
    }
    s->update (item);
    return s;
}

std::shared_ptr<FrameSource> MediaSourceManager::find (int itemId) const
{
    if (auto it = sources.find (itemId); it != sources.end())
        return it->second;
    return nullptr;
}

void MediaSourceManager::keepOnly (const std::set<int>& itemIds)
{
    for (auto it = sources.begin(); it != sources.end();)
        it = itemIds.count (it->first) > 0 ? std::next (it) : sources.erase (it);
}

void MediaSourceManager::setOnAir (const std::set<int>& itemIds)
{
    // Una fuente compartida esta al aire si lo esta cualquiera de sus elementos.
    std::map<FrameSource*, bool> state;
    for (const auto& [id, s] : sources)
        if (s != nullptr)
            state[s.get()] = state[s.get()] || itemIds.count (id) > 0;
    for (const auto& [s, on] : state)
        s->setOnAir (on);
}

void MediaSourceManager::setViewing (bool v)
{
    viewing = v;
    for (const auto& [id, s] : sources)
        if (s != nullptr)
            s->setViewing (v);
}

void MediaSourceManager::tick()
{
    std::set<FrameSource*> done;
    for (const auto& [id, s] : sources)
        if (s != nullptr && done.insert (s.get()).second)
            s->tick();
}
}
