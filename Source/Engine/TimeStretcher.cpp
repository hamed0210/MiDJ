#include "Engine/TimeStretcher.h"

#include <rubberband/RubberBandStretcher.h>

#include <algorithm>

namespace midj
{
using RubberBand::RubberBandStretcher;

TimeStretcher::TimeStretcher() = default;
TimeStretcher::~TimeStretcher() = default;

void TimeStretcher::prepare (double sampleRate, Engine engine)
{
    const auto options = RubberBandStretcher::OptionProcessRealTime
                       | RubberBandStretcher::OptionThreadingNever
                       | (engine == Engine::finer ? RubberBandStretcher::OptionEngineFiner
                                                  : RubberBandStretcher::OptionEngineFaster
                                                        | RubberBandStretcher::OptionTransientsMixed);
    stretcher = std::make_unique<RubberBandStretcher> ((size_t) sampleRate, 2, options, 1.0, 1.0);
    stretcher->setMaxProcessSize (chunkSize);
    input.setSize (2, chunkSize);
    discard.setSize (2, chunkSize);
    currentTempo = 1.0;
    reset();
}

void TimeStretcher::reset()
{
    if (stretcher == nullptr)
        return;
    stretcher->reset();
    stretcher->setTimeRatio (1.0 / currentTempo);

    // Receta de Rubber Band para que la salida quede alineada con la entrada: meter
    // primero "start pad" muestras de silencio y tirar "start delay" muestras de salida.
    input.clear();
    auto pad = (int) stretcher->getPreferredStartPad();
    while (pad > 0)
    {
        const int n = juce::jmin (pad, chunkSize);
        stretcher->process (input.getArrayOfReadPointers(), (size_t) n, false);
        pad -= n;
    }
    samplesToDiscard = (int) stretcher->getStartDelay();
    fadePosition = 0;
}

void TimeStretcher::setTempo (double tempo)
{
    if (tempo != currentTempo && stretcher != nullptr)
    {
        currentTempo = tempo;
        stretcher->setTimeRatio (1.0 / tempo);
    }
}

void TimeStretcher::pull (Source& source)
{
    const auto required = (int) stretcher->getSamplesRequired();
    const int n = juce::jlimit (64, chunkSize, required > 0 ? required : 256);
    source.read (input.getWritePointer (0), input.getWritePointer (1), n);
    stretcher->process (input.getArrayOfReadPointers(), (size_t) n, false);
}

void TimeStretcher::process (float* left, float* right, int n, Source& source)
{
    // Primero se tira el retardo inicial que quedo pendiente tras reset().
    while (samplesToDiscard > 0)
    {
        if (stretcher->available() <= 0)
        {
            pull (source);
            continue;
        }
        const int k = std::min ({ samplesToDiscard, stretcher->available(), chunkSize });
        stretcher->retrieve (discard.getArrayOfWritePointers(), (size_t) k);
        samplesToDiscard -= k;
    }

    int done = 0;
    while (done < n)
    {
        if (stretcher->available() <= 0)
        {
            pull (source);
            continue;
        }
        const int k = juce::jmin (n - done, stretcher->available());
        float* outs[2] = { left + done, right + done };
        stretcher->retrieve (outs, (size_t) k);
        done += k;
    }

    // Fundido corto tras un reset para que el salto no suene a golpe.
    for (int i = 0; i < n && fadePosition < fadeLength; ++i, ++fadePosition)
    {
        const float g = (float) fadePosition / (float) fadeLength;
        left[i] *= g;
        right[i] *= g;
    }
}

int TimeStretcher::getLatencySamples() const
{
    return stretcher != nullptr ? (int) stretcher->getLatency() : 0;
}
}
