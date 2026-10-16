#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>

namespace midj
{
// Un plato: carga una cancion, la reproduce y permite cambiar su velocidad.
// Es una AudioSource, asi que el mezclador le pide bloques de audio.
class Deck : public juce::AudioSource,
             public juce::ChangeBroadcaster
{
public:
    explicit Deck (juce::AudioFormatManager& formatManager);
    ~Deck() override;

    bool load (const juce::File& file);
    void play();
    void pause();
    bool isPlaying() const;

    // 1.0 = velocidad normal. Como un vinilo: cambia tempo y tono a la vez.
    void setSpeed (double ratio);
    double getSpeed() const { return speed.load(); }

    double getPositionSeconds() const;
    void setPositionSeconds (double seconds);
    double getLengthSeconds() const;

    // BPM original del archivo (0 mientras se analiza) y BPM efectivo con la velocidad.
    double getOriginalBpm() const { return bpm.load(); }
    double getCurrentBpm() const  { return bpm.load() * speed.load(); }

    juce::File getFile() const { return loadedFile; }
    juce::AudioThumbnail& getThumbnail() { return thumbnail; }

    // juce::AudioSource
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;

private:
    void startBpmAnalysis (const juce::File& file);

    juce::AudioFormatManager& formats;
    juce::TimeSliceThread readAheadThread { "MiDJ deck read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transport;
    juce::ResamplingAudioSource resampler { &transport, false, 2 };

    juce::AudioThumbnailCache thumbnailCache { 2 };
    juce::AudioThumbnail thumbnail { 512, formats, thumbnailCache };

    juce::ThreadPool analysisPool { 1 };
    juce::File loadedFile;
    std::atomic<double> speed { 1.0 };
    std::atomic<double> bpm { 0.0 };
    std::atomic<int> loadGeneration { 0 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Deck)
};
}
