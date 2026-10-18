#include "Engine/HeadphoneOutput.h"

namespace midj
{
HeadphoneOutput::~HeadphoneOutput()
{
    close();
}

juce::StringArray HeadphoneOutput::outputNames()
{
    std::unique_ptr<juce::AudioIODeviceType> t (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));
    if (t == nullptr)
        return {};
    t->scanForDevices();
    return t->getDeviceNames (false);
}

bool HeadphoneOutput::open (const juce::String& wanted, juce::String& error)
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
    if (! type->getDeviceNames (false).contains (wanted))
    {
        error = "No esta conectada: " + wanted;
        return false;
    }
    device.reset (type->createDevice (wanted, {}));
    if (device == nullptr)
    {
        error = "No se pudo abrir " + wanted;
        return false;
    }
    // A la frecuencia de la salida principal si la admite (asi casi no hay que remuestrear).
    double rate = 0.0;
    for (auto r : device->getAvailableSampleRates())
        if (std::abs (r - sourceRate.load()) < 1.0)
            rate = r;
    juce::BigInteger outputs;
    outputs.setRange (0, juce::jmin (2, device->getOutputChannelNames().size()), true);
    error = device->open ({}, outputs, rate, device->getDefaultBufferSize());
    if (error.isNotEmpty())
    {
        device.reset();
        return false;
    }
    name = wanted;
    device->start (this);
    return true;
}

void HeadphoneOutput::close()
{
    if (device != nullptr)
    {
        device->stop();
        device->close();
        device.reset();
    }
    name.clear();
}

void HeadphoneOutput::prepare (double sampleRate)
{
    sourceRate = sampleRate;
}

void HeadphoneOutput::push (const float* left, const float* right, int n)
{
    // Si nadie la vacia (sin dispositivo o parado), se tira lo que no cabe.
    if (fifo.getFreeSpace() < n)
        return;
    int start1, size1, start2, size2;
    fifo.prepareToWrite (n, start1, size1, start2, size2);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* src = ch == 0 ? left : (right != nullptr ? right : left);
        if (size1 > 0) ring.copyFrom (ch, start1, src, size1);
        if (size2 > 0) ring.copyFrom (ch, start2, src + size1, size2);
    }
    fifo.finishedWrite (size1 + size2);
}

void HeadphoneOutput::audioDeviceAboutToStart (juce::AudioIODevice*)
{
    buffering = true;
}

void HeadphoneOutput::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                                        const juce::AudioIODeviceCallbackContext&)
{
    if (numOut <= 0)
        return;
    pull (out[0], numOut > 1 ? out[1] : nullptr, n, device != nullptr ? device->getCurrentSampleRate() : 0.0);
    for (int ch = 2; ch < numOut; ++ch)
        juce::FloatVectorOperations::clear (out[ch], n);
}

void HeadphoneOutput::pullForTest (float* left, float* right, int n, double outputRate)
{
    pull (left, right, n, outputRate);
}

void HeadphoneOutput::pull (float* left, float* right, int n, double outRate)
{
    juce::FloatVectorOperations::clear (left, n);
    if (right != nullptr)
        juce::FloatVectorOperations::clear (right, n);
    const double inRate = sourceRate.load();
    if (inRate <= 0.0 || outRate <= 0.0)
        return;

    // Cola deseada: 40 ms y un bloque. Vacia: silencio hasta llenarla. Muy llena: se tira lo viejo.
    const int target = (int) (inRate * 0.04) + (int) std::ceil (n * inRate / outRate);
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
    const double drift = juce::jlimit (-1.0, 1.0, (double) (ready - target) / (double) target);
    const double ratio = inRate / outRate * (1.0 + 0.005 * drift);
    const int needed = (int) std::ceil (n * ratio) + 4;
    if (needed > gathered.getNumSamples() || ready < needed)
    {
        buffering = true;
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
    used = interpolators[0].process (ratio, gathered.getReadPointer (0), left, n);
    if (right != nullptr)
        interpolators[1].process (ratio, gathered.getReadPointer (1), right, n);
    fifo.finishedRead (juce::jmin (used, needed));
}
}
