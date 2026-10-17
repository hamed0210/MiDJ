#pragma once

#include "Engine/BpmDetector.h"
#include "Engine/MixPoints.h"
#include "Engine/TimeStretcher.h"

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

    // Carga asincrona: devuelve false si el archivo no se puede abrir. Si se pasa un analisis ya
    // conocido (de la biblioteca) se usa tal cual y la carga es mucho mas rapida.
    bool load (const juce::File& file, TempoAnalysis known = {});
    // Se llama en el hilo de interfaz cuando el deck ha analizado un archivo por su cuenta.
    std::function<void (const juce::File&, const TempoAnalysis&)> onAnalysed;
    juce::File getLoadedFile() const { return loadedFile; }
    // Stems: la voz separada de la cancion cargada (hilo de interfaz). Lo instrumental es
    // original - voz. Se rechaza si no es de la cancion que hay ahora (file) o no mide lo mismo.
    bool setVocals (const juce::File& file, std::shared_ptr<const juce::AudioBuffer<float>> vocals);
    bool hasStems() const                 { return stemsReady.load(); }
    void setVocalsOn (bool on)            { vocalsOn = on; }
    void setInstrumentalOn (bool on)      { instrumentalOn = on; }
    bool isVocalsOn() const               { return vocalsOn.load(); }
    bool isInstrumentalOn() const         { return instrumentalOn.load(); }

    // Puntos de automix de la cancion cargada (se calculan al cargarla; hilo de interfaz).
    const MixPoints& getMixPoints() const { return mixPoints; }
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

    // 1.0 = velocidad normal. Sin keylock, como un vinilo: cambia tempo y tono a la vez.
    // Con keylock cambia solo el tempo y el tono se mantiene.
    void setSpeed (double ratio);
    double getSpeed() const { return speed.load(); }

    // Jog (rueda o encoder): sonando, empuja el tempo un momento como el borde de un plato
    // (el empuje se desvanece en ~0,1 s); parado, mueve la posicion 10 ms por paso.
    void jog (int ticks);
    double getJogBend() const { return jogBend.load(); }

    // Keylock (master tempo): activado por defecto.
    void setKeyLock (bool shouldLock) { keyLock = shouldLock; }
    bool isKeyLocked() const          { return keyLock.load(); }
    // Motor del keylock. Se aplica en el proximo prepareToPlay (al abrir el audio).
    void setKeyLockEngine (TimeStretcher::Engine e) { keyLockEngine = e; }

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
        std::shared_ptr<const juce::AudioBuffer<float>> vocals;   // stem de voz (mismo tamano), si se ha separado
        double sampleRate = 44100.0;
    };

    void install (std::unique_ptr<Track> track, const juce::String& name, TempoAnalysis analysis, MixPoints points);
    void requestSeek (double seconds);
    void renderVinyl (float* left, float* right, int n);
    void renderKeyLocked (float* left, float* right, int n);
    // Avanza una posicion (en muestras del archivo) aplicando el bucle activo.
    double advance (double position, double delta) const;

    // Lee la cancion para el estirador desde feedPosition, a tempo original.
    struct Feeder : TimeStretcher::Source
    {
        explicit Feeder (Deck& d) : deck (d) {}
        void read (float* left, float* right, int n) override;
        Deck& deck;
    };

    juce::AudioFormatManager& formats;
    juce::AudioThumbnailCache thumbnailCache { 2 };
    juce::AudioThumbnail thumbnail { 512, formats, thumbnailCache };
    juce::ThreadPool loaderPool { 1 };

    // La pista activa: el hilo de audio la usa con un try-lock; si la interfaz la esta
    // cambiando en ese instante, ese bloque suena en silencio (nunca se bloquea el audio).
    juce::SpinLock trackLock;
    std::unique_ptr<Track> track;
    juce::String trackName;
    juce::File loadedFile;
    MixPoints mixPoints;

    std::atomic<bool> loading { false }, playing { false }, keyLock { true };
    std::atomic<double> speed { 1.0 }, bpm { 0.0 }, firstBeat { 0.0 }, cuePoint { 0.0 };
    std::atomic<double> positionSeconds { 0.0 }, lengthSeconds { 0.0 };
    std::atomic<double> pendingSeek { -1.0 };            // < 0 = ninguna
    std::atomic<double> loopStart { 0.0 }, loopEnd { 0.0 };
    std::atomic<int> loopBeats { 0 };
    std::atomic<int> loadGeneration { 0 };
    std::atomic<double> jogBend { 0.0 };
    std::atomic<bool> stemsReady { false }, vocalsOn { true }, instrumentalOn { true };   // empuje del jog: velocidad x (1 + jogBend)
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    // Solo del hilo de audio.
    double readPosition = 0.0;      // lo que se oye, en muestras del archivo (fraccionaria)
    double feedPosition = 0.0;      // lo que se ha dado al estirador: va por delante
    double outputRate = 44100.0;
    double fileStep = 1.0;          // muestras de archivo por muestra de salida a tempo 1
    double blockSpeed = 1.0;
    // Stems (hilo de audio): ganancias suavizadas de la voz y de lo instrumental.
    bool stemsActive = false;
    float stemVocal = 1.0f, stemInstrumental = 1.0f, stemVocalTarget = 1.0f, stemInstrumentalTarget = 1.0f, stemCoeff = 0.01f;
    void readFrame (double position, float& left, float& right);        // velocidad de este bloque (tempo y empuje del jog)
    bool loopActive = false;        // copia del bucle para el bloque en curso
    double loopStartSample = 0.0, loopEndSample = 0.0;
    const Track* current = nullptr; // pista del bloque en curso (bajo trackLock)
    bool stretching = false;        // el bloque anterior uso el estirador
    bool stretcherDirty = true;     // hay que reiniciar el estirador antes de usarlo
    TimeStretcher stretcher;
    TimeStretcher::Engine keyLockEngine = TimeStretcher::Engine::faster;   // R3 no da tiempo real en un A8
    Feeder feeder { *this };
    juce::AudioBuffer<float> renderBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Deck)
};
}
