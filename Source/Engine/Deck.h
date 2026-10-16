#pragma once

#include "Engine/BpmDetector.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <memory>

namespace midj
{
// Un plato. Como VirtualDJ, decodifica la cancion entera a memoria al cargarla, asi
// los saltos (CUE, bucles, SYNC) son instantaneos y exactos a la muestra.
//
// Hilos:
//  - interfaz: load, play, cue, setLoop, syncTo... escriben atomicos o peticiones.
//  - audio: getNextAudioBlock lee esos atomicos y es el unico que mueve la posicion.
//  - carga: un hilo aparte decodifica y analiza; el resultado se instala en el hilo de interfaz.
class Deck : public juce::AudioSource,
             public juce::ChangeBroadcaster
{
public:
    explicit Deck (juce::AudioFormatManager& formatManager);
    ~Deck() override;

    // Carga asincrona: devuelve false si el archivo no se puede abrir.
    bool load (const juce::File& file);
    // Carga sincrona desde memoria (pruebas). Analiza el tempo si analysis.bpm == 0.
    void loadBuffer (juce::AudioBuffer<float> audio, double sampleRate, const juce::String& name,
                     TempoAnalysis analysis = {});
    bool isLoading() const { return loading.load(); }
    bool hasTrack() const;

    void play();
    void pause();
    bool isPlaying() const { return playing.load(); }

    // CUE estilo CDJ: parado = fija el punto CUE aqui; sonando = vuelve al CUE y para.
    void cue();
    double getCuePoint() const { return cuePoint.load(); }

    // 1.0 = velocidad normal. Como un vinilo: cambia tempo y tono a la vez.
    void setSpeed (double ratio);
    double getSpeed() const { return speed.load(); }

    double getPositionSeconds() const { return positionSeconds.load(); }
    void setPositionSeconds (double seconds);
    double getLengthSeconds() const { return lengthSeconds.load(); }

    // Tempo: BPM y primer pulso del archivo, BPM efectivo con la velocidad y fase (0-1).
    double getOriginalBpm() const { return bpm.load(); }
    double getCurrentBpm() const  { return bpm.load() * speed.load(); }
    double getFirstBeat() const   { return firstBeat.load(); }
    double getBeatPhase() const;
    // Posicion del pulso n en segundos del archivo.
    double beatTime (double beatIndex) const;

    // SYNC: iguala el tempo al del otro deck y alinea los pulsos. false si falta BPM.
    bool syncTo (const Deck& master);

    // Bucle de N pulsos desde el pulso mas cercano antes de la posicion actual.
    void setLoop (int beats);
    void exitLoop();
    int getLoopBeats() const       { return loopBeats.load(); }
    bool isLooping() const         { return loopBeats.load() > 0; }
    double getLoopStart() const    { return loopStart.load(); }
    double getLoopEnd() const      { return loopEnd.load(); }

    juce::String getTrackName() const { return trackName; }
    juce::AudioThumbnail& getThumbnail() { return thumbnail; }

    // juce::AudioSource
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override {}
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;

private:
    struct Track
    {
        juce::AudioBuffer<float> audio;   // estereo, en el sample rate del archivo
        double sampleRate = 44100.0;
    };

    void install (std::unique_ptr<Track> track, const juce::String& name, TempoAnalysis analysis);
    void requestSeek (double seconds);

    juce::AudioFormatManager& formats;
    juce::AudioThumbnailCache thumbnailCache { 2 };
    juce::AudioThumbnail thumbnail { 512, formats, thumbnailCache };
    juce::ThreadPool loaderPool { 1 };

    // La pista activa: el hilo de audio la usa con un try-lock; si la interfaz la esta
    // cambiando en ese instante, ese bloque suena en silencio (nunca se bloquea el audio).
    juce::SpinLock trackLock;
    std::unique_ptr<Track> track;
    juce::String trackName;

    std::atomic<bool> loading { false }, playing { false };
    std::atomic<double> speed { 1.0 }, bpm { 0.0 }, firstBeat { 0.0 }, cuePoint { 0.0 };
    std::atomic<double> positionSeconds { 0.0 }, lengthSeconds { 0.0 };
    std::atomic<double> pendingSeek { -1.0 };            // < 0 = ninguna
    std::atomic<double> loopStart { 0.0 }, loopEnd { 0.0 };
    std::atomic<int> loopBeats { 0 };
    std::atomic<int> loadGeneration { 0 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    // Solo del hilo de audio.
    double readPosition = 0.0;      // en muestras del archivo, fraccionaria
    double outputRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Deck)
};
}
