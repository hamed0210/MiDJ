#include "Engine/Deck.h"

#include <cmath>

namespace midj
{
namespace
{
    // Interpolacion de Hermite de 4 puntos: suave y barata, buena para cambios de velocidad.
    inline float hermite (const float* d, int n, double pos)
    {
        const int i = (int) pos;
        const float t = (float) (pos - i);
        auto at = [d, n] (int k) { return d[juce::jlimit (0, n - 1, k)]; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }
}

Deck::Deck (juce::AudioFormatManager& formatManager) : formats (formatManager) {}

Deck::~Deck()
{
    *alive = false;
    loaderPool.removeAllJobs (true, 10000);
}

bool Deck::hasTrack() const
{
    return lengthSeconds.load() > 0.0;
}

bool Deck::load (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return false;

    pause();
    loading = true;
    thumbnail.setSource (new juce::FileInputSource (file));
    sendChangeMessage();

    const int generation = ++loadGeneration;
    loaderPool.addJob ([this, file, generation, flag = alive, r = reader.release()]
    {
        std::unique_ptr<juce::AudioFormatReader> rd (r);
        auto newTrack = std::make_unique<Track>();
        newTrack->sampleRate = rd->sampleRate;
        const int length = (int) rd->lengthInSamples;
        newTrack->audio.setSize (2, length);
        // Mono se duplica en los dos canales; mas de dos canales: solo los dos primeros.
        rd->read (&newTrack->audio, 0, length, 0, true, true);
        const auto analysis = BpmDetector::analyse (newTrack->audio, newTrack->sampleRate);

        auto* raw = newTrack.release();
        juce::MessageManager::callAsync ([this, raw, file, analysis, generation, flag]
        {
            std::unique_ptr<Track> t (raw);
            if (flag->load() && generation == loadGeneration.load())   // ignora cargas viejas
                install (std::move (t), file.getFileNameWithoutExtension(), analysis);
        });
    });
    return true;
}

void Deck::loadBuffer (juce::AudioBuffer<float> audio, double sampleRate, const juce::String& name,
                       TempoAnalysis analysis)
{
    auto t = std::make_unique<Track>();
    t->audio.setSize (2, audio.getNumSamples());
    for (int ch = 0; ch < 2; ++ch)
        t->audio.copyFrom (ch, 0, audio, juce::jmin (ch, audio.getNumChannels() - 1), 0, audio.getNumSamples());
    t->sampleRate = sampleRate;
    if (analysis.bpm <= 0.0)
        analysis = BpmDetector::analyse (t->audio, sampleRate);
    install (std::move (t), name, analysis);
}

void Deck::install (std::unique_ptr<Track> newTrack, const juce::String& name, TempoAnalysis analysis)
{
    playing = false;
    const double length = newTrack->audio.getNumSamples() / newTrack->sampleRate;
    {
        const juce::SpinLock::ScopedLockType lock (trackLock);
        std::swap (track, newTrack);
        readPosition = 0.0;
        feedPosition = 0.0;
        stretcherDirty = true;
        pendingSeek = -1.0;
    }   // la pista vieja se libera aqui, fuera del hilo de audio

    trackName = name;
    bpm = analysis.bpm;
    firstBeat = analysis.firstBeat;
    lengthSeconds = length;
    positionSeconds = 0.0;
    cuePoint = 0.0;
    loopBeats = 0;
    loading = false;
    sendChangeMessage();
}

void Deck::play()  { if (hasTrack()) playing = true; }
void Deck::pause() { playing = false; }

void Deck::requestSeek (double seconds)
{
    seconds = juce::jlimit (0.0, juce::jmax (0.0, lengthSeconds.load()), seconds);
    pendingSeek = seconds;
    positionSeconds = seconds;   // la interfaz lo ve al instante aunque el audio no corra
}

void Deck::setPositionSeconds (double seconds)
{
    requestSeek (seconds);
}

void Deck::cue()
{
    if (! hasTrack())
        return;
    if (isPlaying())
    {
        pause();
        requestSeek (cuePoint.load());
    }
    else
    {
        cuePoint = positionSeconds.load();
    }
    sendChangeMessage();
}

void Deck::setSpeed (double ratio)
{
    speed = juce::jlimit (0.5, 2.0, ratio);
}

double Deck::beatTime (double beatIndex) const
{
    return firstBeat.load() + beatIndex * 60.0 / juce::jmax (1.0, bpm.load());
}

double Deck::getBeatPhase() const
{
    if (bpm.load() <= 0.0)
        return 0.0;
    const double beats = (positionSeconds.load() - firstBeat.load()) * bpm.load() / 60.0;
    return beats - std::floor (beats);
}

bool Deck::syncTo (const Deck& master)
{
    if (bpm.load() <= 0.0 || master.getOriginalBpm() <= 0.0 || ! hasTrack())
        return false;

    // 1. Tempo: mismo BPM efectivo que el otro deck.
    setSpeed (master.getCurrentBpm() / bpm.load());

    // 2. Fase: mover la posicion lo minimo (como mucho medio pulso) para que los
    //    golpes de los dos decks caigan a la vez.
    double delta = master.getBeatPhase() - getBeatPhase();
    if (delta > 0.5)  delta -= 1.0;
    if (delta < -0.5) delta += 1.0;
    requestSeek (positionSeconds.load() + delta * 60.0 / bpm.load());
    sendChangeMessage();
    return true;
}

void Deck::setLoop (int beats)
{
    if (beats <= 0 || bpm.load() <= 0.0 || ! hasTrack())
        return;

    // El bucle empieza en el pulso mas cercano a la posicion actual (cuantizado).
    const double period = 60.0 / bpm.load();
    const double beatIndex = std::round ((positionSeconds.load() - firstBeat.load()) / period);
    const double start = juce::jmax (0.0, firstBeat.load() + beatIndex * period);
    loopStart = start;
    loopEnd = start + beats * period;
    loopBeats = beats;   // el hilo de audio mira esto el ultimo
    sendChangeMessage();
}

void Deck::exitLoop()
{
    loopBeats = 0;
    sendChangeMessage();
}

void Deck::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    // Fuera del hilo de audio: aqui se reserva toda la memoria que usara el audio.
    outputRate = sampleRate;
    renderBuffer.setSize (2, juce::jmax (samplesPerBlockExpected, 4096));
    stretcher.prepare (sampleRate, keyLockEngine);
    stretcherDirty = true;
}

double Deck::advance (double position, double delta) const
{
    position += delta;
    if (loopActive && position >= loopEndSample && loopEndSample > loopStartSample)
        position -= (loopEndSample - loopStartSample);   // vuelta exacta a la muestra
    return position;
}

// Sin keylock: se lee la cancion mas deprisa o mas despacio (cambia el tono).
void Deck::renderVinyl (float* left, float* right, int n)
{
    const auto& audio = current->audio;
    const int length = audio.getNumSamples();
    const double step = speed.load() * fileStep;
    for (int i = 0; i < n; ++i)
    {
        readPosition = advance (readPosition, 0.0);
        if (readPosition >= length - 1)
        {
            playing = false;   // fin de la cancion
            std::fill (left + i, left + n, 0.0f);
            std::fill (right + i, right + n, 0.0f);
            return;
        }
        left[i] = hermite (audio.getReadPointer (0), length, readPosition);
        right[i] = hermite (audio.getReadPointer (1), length, readPosition);
        readPosition += step;
    }
}

void Deck::Feeder::read (float* left, float* right, int n)
{
    const auto& audio = deck.current->audio;
    const int length = audio.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        deck.feedPosition = deck.advance (deck.feedPosition, 0.0);
        const bool inside = deck.feedPosition < length - 1;   // pasado el final: silencio
        left[i] = inside ? hermite (audio.getReadPointer (0), length, deck.feedPosition) : 0.0f;
        right[i] = inside ? hermite (audio.getReadPointer (1), length, deck.feedPosition) : 0.0f;
        deck.feedPosition += deck.fileStep;
    }
}

// Con keylock: el estirador recibe la cancion a tempo original y la entrega a 'speed'.
// Lo que se oye avanza a speed * fileStep por muestra, igual que sin keylock, asi
// BPM, fase, SYNC y bucles funcionan igual en los dos modos.
void Deck::renderKeyLocked (float* left, float* right, int n)
{
    const int length = current->audio.getNumSamples();
    if (stretcherDirty)
    {
        feedPosition = readPosition;
        stretcher.setTempo (speed.load());
        stretcher.reset();
        stretcherDirty = false;
    }
    stretcher.setTempo (speed.load());
    stretcher.process (left, right, n, feeder);

    const double step = speed.load() * fileStep;
    for (int i = 0; i < n; ++i)
    {
        readPosition = advance (readPosition, step);
        if (readPosition >= length - 1)
        {
            playing = false;
            stretcherDirty = true;
            std::fill (left + i, left + n, 0.0f);
            std::fill (right + i, right + n, 0.0f);
            return;
        }
    }
}

void Deck::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();

    const juce::SpinLock::ScopedTryLockType lock (trackLock);
    if (! lock.isLocked() || track == nullptr)
        return;

    current = track.get();
    const double rate = current->sampleRate;
    fileStep = rate / outputRate;

    const double seek = pendingSeek.exchange (-1.0);
    if (seek >= 0.0)
    {
        readPosition = seek * rate;
        stretcherDirty = true;
    }

    const bool useStretcher = keyLock.load() && stretcher.isPrepared();
    if (useStretcher != stretching)
    {
        stretching = useStretcher;
        stretcherDirty = true;
    }

    if (playing.load())
    {
        loopActive = loopBeats.load() > 0;
        loopStartSample = loopStart.load() * rate;
        loopEndSample = loopEnd.load() * rate;

        auto& out = *info.buffer;
        for (int done = 0; done < info.numSamples && playing.load();)
        {
            const int n = juce::jmin (info.numSamples - done, renderBuffer.getNumSamples());
            float* left = renderBuffer.getWritePointer (0);
            float* right = renderBuffer.getWritePointer (1);
            if (stretching) renderKeyLocked (left, right, n);
            else            renderVinyl (left, right, n);

            out.copyFrom (0, info.startSample + done, left, n);
            if (out.getNumChannels() > 1)
                out.copyFrom (1, info.startSample + done, right, n);
            done += n;
        }
    }

    positionSeconds = readPosition / rate;
    current = nullptr;
}
}
