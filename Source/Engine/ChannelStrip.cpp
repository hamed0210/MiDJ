#include "Engine/ChannelStrip.h"

#include <cmath>

namespace midj
{
namespace
{
    constexpr double smoothingSeconds = 0.02;   // 20 ms: rapido al oido y sin clics
    constexpr int filterUpdateInterval = 32;    // el corte del filtro se recalcula cada 32 muestras
    constexpr float deadZoneLow = 0.44f, deadZoneHigh = 0.54f;
}

float ChannelStrip::gainFromKnob (float k)
{
    k = juce::jlimit (0.0f, 1.0f, k);
    return k <= 0.5f ? k * 2.0f : 1.0f + (k - 0.5f) * 6.0f;
}

ChannelStrip::FilterSetting ChannelStrip::filterFromKnob (float k)
{
    k = juce::jlimit (0.0f, 1.0f, k);
    if (k < deadZoneLow)
    {
        const float t = k / deadZoneLow;
        return { true, true, 20.0f + 15000.0f * t * t * t };
    }
    if (k > deadZoneHigh)
    {
        const float t = (k - deadZoneHigh) / (1.0f - deadZoneHigh);
        return { true, false, 20.0f + 15000.0f * t * t * t };
    }
    return {};
}

void ChannelStrip::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 2 };

    lowSplit.prepare (spec);
    lowSplit.setCutoffFrequency (lowCrossoverHz);
    highSplit.prepare (spec);
    highSplit.setCutoffFrequency (highCrossoverHz);
    lowAllpass.prepare (spec);
    lowAllpass.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
    lowAllpass.setCutoffFrequency (highCrossoverHz);

    filter.prepare (spec);
    filter.setResonance (1.0f / juce::MathConstants<float>::sqrt2);   // Butterworth: sin pico

    for (auto* s : { &gain, &low, &mid, &high, &volume, &filterSmoothed })
        s->reset (sampleRate, smoothingSeconds);
    reset();
}

void ChannelStrip::reset()
{
    lowSplit.reset();
    highSplit.reset();
    lowAllpass.reset();
    filter.reset();
    gain.setCurrentAndTargetValue (gainFromKnob (gainKnob.load()));
    low.setCurrentAndTargetValue (bandGainFromKnob (lowKnob.load()));
    mid.setCurrentAndTargetValue (bandGainFromKnob (midKnob.load()));
    high.setCurrentAndTargetValue (bandGainFromKnob (highKnob.load()));
    volume.setCurrentAndTargetValue (volumeFromFader (volumeFader.load()));
    filterSmoothed.setCurrentAndTargetValue (filterKnob.load());
    filterActive = false;
    updateFilter (filterKnob.load());
}

void ChannelStrip::updateFilter (float knob)
{
    const auto setting = filterFromKnob (knob);
    if (! setting.active)
    {
        filterActive = false;
        return;
    }
    if (! filterActive)
        filter.reset();   // al salir de la zona muerta se empieza sin restos viejos
    filterActive = true;
    filter.setType (setting.lowPass ? juce::dsp::StateVariableTPTFilterType::lowpass
                                    : juce::dsp::StateVariableTPTFilterType::highpass);
    filter.setCutoffFrequency (juce::jmin (setting.cutoffHz, (float) (sampleRate * 0.45)));
}

void ChannelStrip::process (juce::AudioBuffer<float>& buffer, int start, int n)
{
    gain.setTargetValue (gainFromKnob (gainKnob.load()));
    low.setTargetValue (bandGainFromKnob (lowKnob.load()));
    mid.setTargetValue (bandGainFromKnob (midKnob.load()));
    high.setTargetValue (bandGainFromKnob (highKnob.load()));
    volume.setTargetValue (volumeFromFader (volumeFader.load()));
    filterSmoothed.setTargetValue (filterKnob.load());

    const int channels = juce::jmin (2, buffer.getNumChannels());
    float* data[2] = { buffer.getWritePointer (0), buffer.getWritePointer (channels > 1 ? 1 : 0) };

    for (int i = 0; i < n; ++i)
    {
        if (i % filterUpdateInterval == 0)
        {
            updateFilter (filterSmoothed.getCurrentValue());
            filterSmoothed.skip (filterUpdateInterval);
        }
        const float g = gain.getNextValue(), gl = low.getNextValue(), gm = mid.getNextValue(),
                    gh = high.getNextValue(), v = volume.getNextValue();

        for (int ch = 0; ch < channels; ++ch)
        {
            const float x = data[ch][start + i] * g;
            // Con las tres bandas en plano la suma tiene amplitud 1 en todas las frecuencias
            // (solo gira la fase, inaudible); con una banda a 0 esa banda se corta del todo.
            float lo, rest, md, hi;
            lowSplit.processSample (ch, x, lo, rest);
            highSplit.processSample (ch, rest, md, hi);
            lo = lowAllpass.processSample (ch, lo);
            float y = lo * gl + md * gm + hi * gh;
            if (filterActive)
                y = filter.processSample (ch, y);
            data[ch][start + i] = y * v;
        }
    }
    lowSplit.snapToZero();
    highSplit.snapToZero();
    lowAllpass.snapToZero();
    filter.snapToZero();
}
}
