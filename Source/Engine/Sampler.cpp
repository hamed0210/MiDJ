#include "Engine/Sampler.h"

#include <cmath>

namespace midj
{
namespace
{
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

    inline double fraction (double x) { return x - std::floor (x); }
}

Sampler::Sampler() = default;

Sampler::~Sampler()
{
    cancelPendingUpdate();
}

void Sampler::prepare (double sampleRate, int)
{
    outputRate = sampleRate;
    fadeStep = (float) (1.0 / (128.0 * sampleRate / 44100.0));   // 128 muestras a 44,1 kHz
    recordBuffer.setSize (2, (int) (maxRecordSeconds * sampleRate));
}

double Sampler::syncSpeed (double deckBpm, double sampleBpm, double* beatFactor)
{
    double factor = 1.0;
    if (deckBpm <= 0.0 || sampleBpm <= 0.0)
    {
        if (beatFactor != nullptr)
            *beatFactor = 1.0;
        return 1.0;
    }
    double speed = deckBpm / sampleBpm;
    while (speed > 1.5)  { speed *= 0.5; factor *= 0.5; }
    while (speed < 0.66) { speed *= 2.0; factor *= 2.0; }
    if (beatFactor != nullptr)
        *beatFactor = factor;
    return speed;
}

void Sampler::setSample (int pad, std::shared_ptr<SampleData> sample)
{
    auto& p = pads[(size_t) pad];
    p.wanted = false;
    if (sample != nullptr)
    {
        p.loop = sample->loop;
        p.tempoMode = sample->tempoMode;
    }
    {
        const juce::SpinLock::ScopedLockType sl (samplesLock);
        std::swap (p.sample, sample);
    }   // el sonido viejo se libera aqui, en el hilo de interfaz
    if (recordPad.load() == pad && sample != nullptr)
        cancelRecording();
}

std::shared_ptr<const SampleData> Sampler::getSample (int pad) const
{
    const juce::SpinLock::ScopedLockType sl (samplesLock);
    return pads[(size_t) pad].sample;
}

bool Sampler::saveBank (const juce::File& xmlFile, const juce::File& recordingsFolder)
{
    juce::XmlElement xml ("SAMPLERBANK");
    xml.setAttribute ("volume", (double) volume.load());
    for (int i = 0; i < numPads; ++i)
    {
        std::shared_ptr<SampleData> s;
        {
            const juce::SpinLock::ScopedLockType sl (samplesLock);
            s = pads[(size_t) i].sample;
        }
        if (s == nullptr)
            continue;
        if (s->source == juce::File())   // grabado: se guarda una vez como WAV
        {
            recordingsFolder.createDirectory();
            const auto file = recordingsFolder.getNonexistentChildFile (s->name.replaceCharacter (' ', '_'), ".wav", false);
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (new juce::FileOutputStream (file), s->sampleRate, 2, 24, {}, 0));
            if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (s->audio, 0, s->audio.getNumSamples()))
                continue;
            s->source = file;   // el hilo de audio no mira 'source'
        }
        auto* e = xml.createNewChildElement ("PAD");
        e->setAttribute ("index", i);
        e->setAttribute ("path", s->source.getFullPathName());
        e->setAttribute ("name", s->name);
        e->setAttribute ("bpm", s->bpm);
        e->setAttribute ("firstbeat", s->firstBeat);
        e->setAttribute ("trigger", pads[(size_t) i].trigger.load());
        e->setAttribute ("loop", pads[(size_t) i].loop.load() ? 1 : 0);
        e->setAttribute ("tempo", pads[(size_t) i].tempoMode.load());
    }
    xmlFile.getParentDirectory().createDirectory();
    return xml.writeTo (xmlFile);
}

bool Sampler::loadBank (const juce::File& xmlFile, juce::AudioFormatManager& formats)
{
    const auto xml = juce::XmlDocument::parse (xmlFile);
    if (xml == nullptr || ! xml->hasTagName ("SAMPLERBANK"))
        return false;
    setVolume ((float) xml->getDoubleAttribute ("volume", 0.8));
    for (auto* e : xml->getChildWithTagNameIterator ("PAD"))
    {
        const int i = e->getIntAttribute ("index", -1);
        if (i < 0 || i >= numPads)
            continue;
        auto s = SampleData::loadFile (juce::File (e->getStringAttribute ("path")), formats);
        if (s == nullptr)
            continue;   // el archivo ya no esta
        s->name = e->getStringAttribute ("name", s->name);
        if (e->getDoubleAttribute ("bpm") > 0.0)   // las grabaciones son WAV sin tempo: se guarda aparte
        {
            s->bpm = e->getDoubleAttribute ("bpm");
            s->firstBeat = e->getDoubleAttribute ("firstbeat");
        }
        setSample (i, s);
        setTrigger (i, (Trigger) juce::jlimit (0, 2, e->getIntAttribute ("trigger")));
        setLoop (i, e->getIntAttribute ("loop") != 0);
        setTempoMode (i, (TempoMode) juce::jlimit (0, 2, e->getIntAttribute ("tempo")));
    }
    return true;
}

void Sampler::press (int pad)
{
    auto& p = pads[(size_t) pad];
    switch ((Trigger) p.trigger.load())
    {
        case Trigger::onOff:   p.wanted = ! p.wanted.load(); break;
        case Trigger::hold:    p.wanted = true; break;
        case Trigger::stutter: p.wanted = true; ++p.restarts; break;
    }
}

void Sampler::release (int pad)
{
    auto& p = pads[(size_t) pad];
    if ((Trigger) p.trigger.load() == Trigger::hold)
        p.wanted = false;
}

void Sampler::stopAll()
{
    for (auto& p : pads)
        p.wanted = false;
}

void Sampler::startRecording (int pad, int deckIndex, int beats)
{
    recordState = 0;
    recordDeck = deckIndex;
    recordBeats = juce::jlimit (1, 64, beats);
    recordPad = pad;
    recordState = 1;   // el hilo de audio empieza en el proximo pulso del deck
}

void Sampler::cancelRecording()
{
    recordState = 0;
    recordPad = -1;
}

int Sampler::getRecordingPad() const
{
    return recordState.load() != 0 ? recordPad.load() : -1;
}

void Sampler::capture (int deckIndex, const float* left, const float* right, int n, const TempoInfo& tempo)
{
    const int state = recordState.load();
    if ((state != 1 && state != 2) || deckIndex != recordDeck.load())
        return;

    int from = 0;
    if (state == 1)
    {
        if (! tempo.synced)
            return;   // el deck tiene que sonar y tener BPM
        // Primer cruce de un pulso entero dentro de este bloque.
        const double next = std::ceil (tempo.beatAtStart);
        const double offset = (next - tempo.beatAtStart) * tempo.samplesPerBeat;
        if (offset >= n)
            return;
        from = juce::jlimit (0, n - 1, (int) std::lround (offset));
        recordBpm = outputRate * 60.0 / tempo.samplesPerBeat;
        recordTarget = juce::jmin (recordBuffer.getNumSamples(), (int) std::lround (recordBeats.load() * tempo.samplesPerBeat));
        recordWritten = 0;
        recordState = 2;
    }

    const int k = juce::jmin (n - from, recordTarget - recordWritten);
    recordBuffer.copyFrom (0, recordWritten, left + from, k);
    recordBuffer.copyFrom (1, recordWritten, right + from, k);
    recordWritten += k;
    if (recordWritten >= recordTarget)
    {
        recordState = 3;
        triggerAsyncUpdate();   // el sonido se crea en el hilo de interfaz (reserva memoria)
    }
}

void Sampler::handleAsyncUpdate()
{
    if (recordState.load() != 3)
        return;
    const int pad = recordPad.load();
    auto s = std::make_shared<SampleData>();
    s->audio.setSize (2, recordTarget);
    for (int ch = 0; ch < 2; ++ch)
        s->audio.copyFrom (ch, 0, recordBuffer, ch, 0, recordTarget);
    s->sampleRate = outputRate;
    s->bpm = recordBpm;
    s->firstBeat = 0.0;
    s->loop = true;
    s->tempoMode = (int) TempoMode::locked;
    s->name = juce::String ("Grabado ") + (recordDeck.load() == 0 ? "A" : "B") + " " + juce::String (recordBeats.load()) + "p";
    recordState = 0;
    recordPad = -1;
    setSample (pad, s);
    setTrigger (pad, Trigger::onOff);
    if (onRecordingFinished)
        onRecordingFinished (pad);
}

double Sampler::startPosition (const Pad& pad, const SampleData& s, const Reference& ref, double factor) const
{
    const auto mode = (TempoMode) pad.tempoMode.load();
    if (mode != TempoMode::locked || ! ref.valid || s.bpm <= 0.0)
        return 0.0;
    // Bloqueado: el pulso del sonido coincide con el del deck (por compases si es un bucle).
    const double samplePeriod = 60.0 / s.bpm * s.sampleRate;
    const double sampleBeat = ref.beatAtStart * factor;
    const double beats = pad.loop.load() ? (double) s.lengthBeats() : 1.0;
    return s.firstBeat * s.sampleRate + fraction (sampleBeat / beats) * beats * samplePeriod;
}

void Sampler::process (float* left, float* right, int n, const Reference& ref)
{
    const juce::SpinLock::ScopedTryLockType sl (samplesLock);
    if (! sl.isLocked())
        return;   // la interfaz esta cambiando un sonido justo ahora: este bloque sin sampler

    const float master = volume.load();
    for (auto& p : pads)
    {
        const SampleData* s = p.sample.get();
        if (s != p.current)   // sonido nuevo o quitado
        {
            p.current = s;
            p.active = false;
            p.gain = 0.0f;
            p.seenRestarts = p.restarts.load();
        }
        if (s == nullptr)
        {
            p.playing = false;
            p.progress = 0.0;
            continue;
        }

        const int length = s->audio.getNumSamples();
        const bool loop = p.loop.load();
        const auto mode = (TempoMode) p.tempoMode.load();
        const bool follows = mode != TempoMode::free && ref.valid && s->bpm > 0.0;
        double factor = 1.0;
        const double speed = follows ? syncSpeed (ref.bpm, s->bpm, &factor) : 1.0;
        const double step = speed * s->sampleRate / outputRate;

        // Peticiones del usuario.
        const int restarts = p.restarts.load();
        if (restarts != p.seenRestarts)
        {
            p.seenRestarts = restarts;
            if (p.active)
                p.restartPending = true;   // fundido de salida y vuelta a empezar
        }
        const bool wanted = p.wanted.load();
        if (wanted && ! p.active)
        {
            p.active = true;
            p.restartPending = false;
            p.position = startPosition (p, *s, ref, factor);
            p.gain = 0.0f;
        }
        if (! p.active)
        {
            p.playing = false;
            continue;
        }
        p.target = wanted && ! p.restartPending ? 1.0f : 0.0f;

        const float* inL = s->audio.getReadPointer (0);
        const float* inR = s->audio.getReadPointer (1);
        const float level = master * s->gain;
        const bool lockedLoop = mode == TempoMode::locked && loop && follows;
        const double loopSamples = lockedLoop ? s->lengthBeats() * 60.0 / s->bpm * s->sampleRate : (double) length;
        const double samplePeriod = follows ? 60.0 / s->bpm * s->sampleRate : 0.0;

        for (int i = 0; i < n; ++i)
        {
            // Fundido de 128 muestras hacia el objetivo.
            if (p.gain < p.target)      p.gain = juce::jmin (p.target, p.gain + fadeStep);
            else if (p.gain > p.target) p.gain = juce::jmax (p.target, p.gain - fadeStep);
            if (p.gain <= 0.0f && p.target <= 0.0f)
            {
                if (p.restartPending)
                {
                    p.restartPending = false;
                    p.position = startPosition (p, *s, ref, factor);
                    p.target = 1.0f;
                }
                else
                {
                    p.active = false;
                    break;
                }
            }

            double pos = p.position;
            if (lockedLoop)
            {
                // Bucle bloqueado: la posicion sale del pulso del deck, sin deriva.
                const double sampleBeat = (ref.beatAtStart + i / ref.samplesPerBeat) * factor;
                const double beats = (double) s->lengthBeats();
                pos = s->firstBeat * s->sampleRate + fraction (sampleBeat / beats) * beats * samplePeriod;
                while (pos >= length)
                    pos -= loopSamples;
            }
            left[i] += hermite (inL, length, pos) * p.gain * level;
            right[i] += hermite (inR, length, pos) * p.gain * level;

            p.position += step;
            if (p.position >= loopSamples - 1.0 || p.position >= length - 1)
            {
                if (loop)
                    p.position = std::fmod (p.position, loopSamples);
                else
                {
                    // Fin del sonido: se apaga solo (con el pad listo para volver a dispararse).
                    p.wanted = false;
                    p.target = 0.0f;
                    p.position = length - 1.0;
                }
            }
        }
        p.playing = p.active && p.target > 0.0f;
        p.progress = p.active ? juce::jlimit (0.0, 1.0, (lockedLoop ? std::fmod (p.position, loopSamples) : p.position) / length) : 0.0;
    }
}
}
