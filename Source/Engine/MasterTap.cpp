#include "Engine/MasterTap.h"

namespace midj
{
void MasterTap::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 44100.0;
}

void MasterTap::push (const float* left, const float* right, int n)
{
    auto pos = written.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i, ++pos)
        ring[(size_t) (pos & (capacity - 1))] = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
    written.store (pos, std::memory_order_release);

    seq.fetch_add (1, std::memory_order_acq_rel);
    blockMs.store (juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
    blockEnd.store (pos, std::memory_order_relaxed);
    seq.fetch_add (1, std::memory_order_acq_rel);
}

void MasterTap::setBeat (bool valid, double beat, double bpm)
{
    seq.fetch_add (1, std::memory_order_acq_rel);
    beatValid.store (valid, std::memory_order_relaxed);
    beatAtEnd.store (beat, std::memory_order_relaxed);
    beatBpm.store (bpm, std::memory_order_relaxed);
    seq.fetch_add (1, std::memory_order_acq_rel);
}

juce::int64 MasterTap::heardPosition (double nowMs) const
{
    double ms = 0.0;
    juce::int64 end = 0;
    for (int tries = 0; tries < 8; ++tries)
    {
        const auto s = seq.load (std::memory_order_acquire);
        ms = blockMs.load (std::memory_order_relaxed);
        end = blockEnd.load (std::memory_order_relaxed);
        if ((s & 1) == 0 && seq.load (std::memory_order_acquire) == s)
            break;
    }
    // Desde el ultimo bloque ha seguido sonando (como mucho lo ya escrito); menos la latencia.
    const auto elapsed = (juce::int64) (juce::jmax (0.0, nowMs - ms) * 0.001 * rate.load());
    const auto pos = end + elapsed - latency.load();
    return juce::jlimit ((juce::int64) 0, written.load (std::memory_order_acquire), pos);
}

void MasterTap::read (float* dest, int n, double nowMs) const
{
    n = juce::jmin (n, capacity);
    const auto end = heardPosition (nowMs);
    const auto oldest = juce::jmax ((juce::int64) 0, written.load (std::memory_order_acquire) - capacity);
    for (int i = 0; i < n; ++i)
    {
        const auto p = end - n + i;
        dest[i] = p >= oldest ? ring[(size_t) (p & (capacity - 1))] : 0.0f;
    }
}

MasterTap::Beat MasterTap::beatAt (double nowMs) const
{
    Beat b;
    double ms = 0.0;
    for (int tries = 0; tries < 8; ++tries)
    {
        const auto s = seq.load (std::memory_order_acquire);
        b.valid = beatValid.load (std::memory_order_relaxed);
        b.beat = beatAtEnd.load (std::memory_order_relaxed);
        b.bpm = beatBpm.load (std::memory_order_relaxed);
        ms = blockMs.load (std::memory_order_relaxed);
        if ((s & 1) == 0 && seq.load (std::memory_order_acquire) == s)
            break;
    }
    if (b.valid && b.bpm > 0.0)
    {
        const double seconds = juce::jmax (0.0, nowMs - ms) * 0.001 - latency.load() / rate.load();
        b.beat += seconds * b.bpm / 60.0;
    }
    return b;
}
}
