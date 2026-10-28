#include "Engine/AuxAudio.h"

namespace midj
{
// --- Bus ---------------------------------------------------------------------------------------

void AuxBus::add (AuxSource* source)
{
    const juce::ScopedLock sl (lock);
    if (std::find (sources.begin(), sources.end(), source) != sources.end())
        return;
    if (rate > 0.0)
        source->prepare (rate, block);   // el audio ya va: se prepara antes de que lo use
    sources.push_back (source);
}

void AuxBus::remove (AuxSource* source)
{
    const juce::ScopedLock sl (lock);
    std::erase (sources, source);
}

void AuxBus::prepare (double sampleRate, int blockSize)
{
    const juce::ScopedLock sl (lock);
    rate = sampleRate;
    block = blockSize;
    for (auto* s : sources)
        s->prepare (sampleRate, blockSize);
}

void AuxBus::addTo (float* left, float* right, int n)
{
    // Hilo de audio: si la interfaz esta anadiendo o preparando una fuente (abrir un video puede
    // tardar), este bloque va sin ellas en vez de esperar y cortar el master.
    const juce::ScopedTryLock sl (lock);
    if (! sl.isLocked())
        return;
    for (auto* s : sources)
        s->addTo (left, right, n);
}

// --- Entrada de audio --------------------------------------------------------------------------

AudioInputSource::~AudioInputSource()
{
    close();
}

juce::StringArray AudioInputSource::inputNames()
{
    std::unique_ptr<juce::AudioIODeviceType> t (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));
    if (t == nullptr)
        return {};
    t->scanForDevices();
    return t->getDeviceNames (true);
}

bool AudioInputSource::open (const juce::String& wanted, juce::String& error)
{
    if (device != nullptr && wanted == name)
        return true;
    close();
    if (wanted.isEmpty())
        return true;
    if (type == nullptr)
        type.reset (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));
    if (type == nullptr)
    {
        error = "WASAPI no esta disponible";
        return false;
    }
    type->scanForDevices();
    if (! type->getDeviceNames (true).contains (wanted))
    {
        error = "No esta conectada: " + wanted;
        return false;
    }
    device.reset (type->createDevice ({}, wanted));
    if (device == nullptr)
    {
        error = "No se pudo abrir " + wanted;
        return false;
    }
    // A la frecuencia de la salida si la admite (asi casi no hay que remuestrear).
    double rate = 0.0;
    for (auto r : device->getAvailableSampleRates())
        if (std::abs (r - outputRate) < 1.0)
            rate = r;
    juce::BigInteger inputs;
    inputs.setRange (0, juce::jmin (2, device->getInputChannelNames().size()), true);
    error = device->open (inputs, {}, rate, device->getDefaultBufferSize());
    if (error.isNotEmpty())
    {
        device.reset();
        return false;
    }
    name = wanted;
    inputRate = device->getCurrentSampleRate();
    flush = true;
    device->start (this);
    return true;
}

void AudioInputSource::close()
{
    if (device != nullptr)
    {
        device->stop();
        device->close();
        device.reset();
    }
    name = {};
    level = 0.0f;
}

void AudioInputSource::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    inputRate = d->getCurrentSampleRate();
    flush = true;
}

void AudioInputSource::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                                         const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch] != nullptr)
            juce::FloatVectorOperations::clear (out[ch], n);
    if (numIn <= 0 || in[0] == nullptr)
        return;
    push (in[0], numIn > 1 && in[1] != nullptr ? in[1] : in[0], n);
}

void AudioInputSource::pushForTest (const float* left, const float* right, int n, double rate)
{
    inputRate = rate;
    push (left, right != nullptr ? right : left, n);
}

void AudioInputSource::push (const float* left, const float* right, int n)
{
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = juce::jmax (peak, std::abs (left[i]), std::abs (right[i]));
    level = juce::jmax (peak, level.load() * 0.8f);

    // Si la salida no lee (p. ej. el audio esta parado), lo que no cabe se pierde.
    int start1, size1, start2, size2;
    fifo.prepareToWrite (n, start1, size1, start2, size2);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* src = ch == 0 ? left : right;
        if (size1 > 0) ring.copyFrom (ch, start1, src, size1);
        if (size2 > 0) ring.copyFrom (ch, start2, src + size1, size2);
    }
    fifo.finishedWrite (size1 + size2);
}

void AudioInputSource::prepare (double sampleRate, int blockSize)
{
    outputRate = sampleRate;
    // Bastante para un bloque remuestreado desde una entrada al doble de frecuencia.
    gathered.setSize (2, blockSize * 4 + 64, false, true, true);
    converted.setSize (2, blockSize + 16, false, true, true);
    flush = true;
}

void AudioInputSource::addTo (float* left, float* right, int n)
{
    const double inRate = inputRate.load();
    if (inRate <= 0.0 || outputRate <= 0.0)
        return;
    if (flush.exchange (false))
    {
        fifo.finishedRead (fifo.getNumReady());
        buffering = true;
    }

    const int chunk = juce::jmin (n, converted.getNumSamples() - 16);
    for (int done = 0; done < n; done += chunk)
    {
        const int count = juce::jmin (chunk, n - done);
        // Cola deseada: 40 ms y un bloque. Vacia: se espera a llenarla (silencio). Muy llena
        // (la salida estuvo parada): se tira lo viejo.
        const int target = (int) (inRate * 0.04) + (int) std::ceil (count * inRate / outputRate);
        int ready = fifo.getNumReady();
        if (buffering)
        {
            if (ready < target)
                return;
            buffering = false;
            for (auto& i : interpolators)
                i.reset();
        }
        if (ready > target * 4)
        {
            fifo.finishedRead (ready - target);
            ready = target;
        }
        // Un poco mas deprisa si la cola crece y mas despacio si mengua (hasta 0,5 %, no se oye).
        const double drift = juce::jlimit (-1.0, 1.0, (double) (ready - target) / (double) target);
        const double ratio = inRate / outputRate * (1.0 + 0.005 * drift);
        const int needed = juce::jmin (gathered.getNumSamples(), (int) std::ceil (count * ratio) + 4);
        if (ready < needed)
        {
            buffering = true;   // se vacio: se espera a que se llene otra vez
            return;
        }
        int start1, size1, start2, size2;
        fifo.prepareToRead (needed, start1, size1, start2, size2);
        for (int ch = 0; ch < 2; ++ch)
        {
            if (size1 > 0) gathered.copyFrom (ch, 0, ring, ch, start1, size1);
            if (size2 > 0) gathered.copyFrom (ch, size1, ring, ch, start2, size2);
        }
        int used = 0;
        for (int ch = 0; ch < 2; ++ch)
            used = interpolators[(size_t) ch].process (ratio, gathered.getReadPointer (ch), converted.getWritePointer (ch), count);
        fifo.finishedRead (juce::jmin (used, needed));

        const float g = gain.load();
        juce::FloatVectorOperations::addWithMultiply (left + done, converted.getReadPointer (0), g, count);
        if (right != nullptr)
            juce::FloatVectorOperations::addWithMultiply (right + done, converted.getReadPointer (1), g, count);
    }
}
}
