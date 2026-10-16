#include "Engine/Deck.h"
#include "Engine/BpmDetector.h"

namespace midj
{
Deck::Deck (juce::AudioFormatManager& formatManager)
    : formats (formatManager)
{
    readAheadThread.startThread (juce::Thread::Priority::high);
}

Deck::~Deck()
{
    *alive = false;
    analysisPool.removeAllJobs (true, 5000);
    transport.setSource (nullptr);
    readAheadThread.stopThread (2000);
}

bool Deck::load (const juce::File& file)
{
    auto* reader = formats.createReaderFor (file);
    if (reader == nullptr)
        return false;

    pause();
    auto newSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    // 32768 muestras de lectura anticipada en un hilo aparte: el hilo de audio nunca toca el disco.
    transport.setSource (newSource.get(), 32768, &readAheadThread, reader->sampleRate);
    readerSource = std::move (newSource);

    loadedFile = file;
    thumbnail.setSource (new juce::FileInputSource (file));
    bpm = 0.0;
    startBpmAnalysis (file);
    sendChangeMessage();
    return true;
}

void Deck::startBpmAnalysis (const juce::File& file)
{
    const int generation = ++loadGeneration;
    analysisPool.addJob ([this, file, generation]
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
            return;
        const double detected = BpmDetector::detect (*reader);
        if (generation == loadGeneration.load())   // ignora resultados de una carga anterior
        {
            bpm = detected;
            // El aviso va al hilo de la interfaz; 'alive' evita usar el deck si ya se destruyo.
            juce::MessageManager::callAsync ([this, flag = alive] { if (flag->load()) sendChangeMessage(); });
        }
    });
}

void Deck::play()            { if (readerSource != nullptr) transport.start(); }
void Deck::pause()           { transport.stop(); }
bool Deck::isPlaying() const { return transport.isPlaying(); }

void Deck::setSpeed (double ratio)
{
    speed = juce::jlimit (0.5, 2.0, ratio);
    // Leer el archivo mas rapido = reproducirlo mas rapido.
    resampler.setResamplingRatio (speed.load());
}

double Deck::getPositionSeconds() const     { return transport.getCurrentPosition(); }
void Deck::setPositionSeconds (double s)    { transport.setPosition (s); }
double Deck::getLengthSeconds() const       { return transport.getLengthInSeconds(); }

void Deck::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    resampler.prepareToPlay (samplesPerBlockExpected, sampleRate);
}

void Deck::releaseResources()
{
    resampler.releaseResources();
}

void Deck::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    if (readerSource == nullptr)
    {
        info.clearActiveBufferRegion();
        return;
    }
    resampler.getNextAudioBlock (info);
}
}
