#pragma once

#include "Engine/AuxAudio.h"
#include "Video/GpuFrames.h"
#include "Video/MediaFoundation.h"
#include "Video/Scenes.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <map>
#include <mutex>
#include <set>

namespace midj
{
// Una fuente de escena con imagen propia que va cambiando (archivo multimedia, capturadora):
// un hilo deja listo el ultimo fotograma y el compositor lo sube a la GPU cuando cambia.
class FrameSource
{
public:
    virtual ~FrameSource() = default;

    // Cualquier hilo: el ultimo fotograma (no valido = no se ve nada) y su numero.
    juce::Image getFrame() const;
    juce::uint64 getFrameNumber() const { return frameNumber.load(); }
    juce::Point<int> getSize() const { return { width.load(), height.load() }; }
    // Los fotogramas que se quedan en la tarjeta grafica (si el decodificador usa la del
    // compositor); si tiene imagen, se dibuja esta en vez de getFrame().
    const std::shared_ptr<GpuFrameSource>& getGpu() const { return gpu; }

    const juce::String& getKey() const { return key; }
    virtual juce::String getStatus() const = 0;   // para las propiedades (vacio = todo bien)

    // Hilo de interfaz: los ajustes que cambian sin volver a abrir (volumen, bucle...).
    virtual void update (const SceneItem& item) = 0;
    // Hilo de interfaz: si sale en la escena al aire (en OBS, "activa") y si alguien mira el video.
    virtual void setOnAir (bool onAir) = 0;
    virtual void setViewing (bool viewing) = 0;
    // Hilo de interfaz, unas veces por segundo.
    virtual void tick() {}

protected:
    void publish (const juce::Image& image);   // no valida = ocultar
    void publishGpu (std::shared_ptr<GpuVideoFrame> frame);
    juce::String key;

private:
    mutable juce::SpinLock lock;
    juce::Image latest;
    std::atomic<juce::uint64> frameNumber { 1 };
    std::atomic<int> width { 0 }, height { 0 };
    std::shared_ptr<GpuFrameSource> gpu = std::make_shared<GpuFrameSource>();
};

// Archivo de video o audio, como la fuente Multimedia de OBS: suena por el master y la imagen
// sigue al sonido (sin pista de audio, a un reloj propio).
class MediaFileSource : public FrameSource,
                        public AuxSource,
                        private juce::Thread
{
public:
    MediaFileSource (const SceneItem& item, AuxBus& bus, juce::AudioFormatManager& formats,
                     juce::TimeSliceThread& readAhead, int maxWidth);
    ~MediaFileSource() override;

    static juce::String keyFor (const SceneItem& item);
    static juce::String fileFilter();   // para elegir el archivo

    juce::String getStatus() const override;
    void update (const SceneItem& item) override;
    void setOnAir (bool onAir) override;
    void setViewing (bool viewing) override;
    void tick() override;

    // Estado (cualquier hilo).
    double getPosition() const;
    double getDuration() const { return duration.load(); }
    bool isPlaying() const { return playing.load(); }
    bool isOpen() const { return opened; }
    bool hasAudio() const { return audioOpen.load(); }

    // Controles de reproduccion (hilo de interfaz), como los de OBS.
    void play();
    void pause();
    void restart();
    void stop();

    void prepare (double sampleRate, int blockSize) override;
    void addTo (float* left, float* right, int n) override;

private:
    void run() override;
    void openFile();
    void closeFile();
    void setClockPosition (double seconds);

    const juce::File file;
    const bool allowGpu;
    const int maxWidth;
    AuxBus& bus;
    juce::AudioFormatManager& formats;
    juce::TimeSliceThread& readAhead;

    // Ajustes
    bool loop = false, restartOnActivate = true, clearOnEnd = true, closeWhenInactive = false;
    std::atomic<bool> looping { false };   // copia de 'loop' para el hilo de video
    std::atomic<double> speed { 1.0 };
    mutable juce::SpinLock positionLock;   // el hilo de video lee la posicion mientras se cierra el archivo
    std::atomic<float> gain { 1.0f };

    // Estado (hilo de interfaz)
    bool onAir = false, opened = false, ended = false;

    // Sonido: archivo -> lectura anticipada -> transporte -> velocidad (hilo de audio).
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transport;
    juce::ResamplingAudioSource resampler { &transport, false, 2 };
    juce::AudioBuffer<float> scratch;
    std::atomic<bool> audioOpen { false };

    // Reloj sin sonido: posicion = base + tiempo pasado x velocidad.
    std::atomic<double> clockBase { 0.0 }, clockStartMs { 0.0 };
    std::atomic<bool> playing { false };

    // Hilo de video
    std::atomic<bool> wantOpen { false }, viewing { false }, hidden { false };
    std::atomic<double> duration { 0.0 };
    std::atomic<bool> videoFailed { false };
    juce::String videoError;   // lo escribe el hilo de video antes de videoFailed
};

// Capturadora de video (o webcam), como el Dispositivo de captura de video de OBS: video por
// Media Foundation y su sonido por una entrada de audio de Windows (en las capturadoras USB/HDMI
// el audio HDMI aparece como un micro aparte).
class CaptureDeviceSource : public FrameSource,
                            private juce::Thread
{
public:
    CaptureDeviceSource (const SceneItem& item, AuxBus& bus, int maxWidth);
    ~CaptureDeviceSource() override;

    static juce::String keyFor (const SceneItem& item);
    // La entrada de audio que parece de este dispositivo (mismo nombre o "capture"/"HDMI"); "" si ninguna.
    static juce::String guessAudioInput (const juce::String& deviceName, const juce::StringArray& inputs);

    juce::String getStatus() const override;
    void update (const SceneItem& item) override;
    void setOnAir (bool onAir) override;
    void setViewing (bool viewing) override;
    float getAudioLevel() const { return audio.getLevel(); }

private:
    void run() override;
    bool shouldRun() const;
    void updateAudio();

    const juce::String deviceId;
    const VideoCaptureReader::Format format;
    const bool allowGpu;
    const int maxWidth;
    AuxBus& bus;

    std::atomic<bool> flip { false }, active { true }, deactivateWhenHidden { false };
    std::atomic<bool> onAir { false }, viewing { false };

    AudioInputSource audio;
    juce::String audioWanted, audioError;
    bool audioRegistered = false;

    mutable juce::SpinLock statusLock;
    juce::String status;
    std::mutex readerLock;
    VideoCaptureReader* current = nullptr;   // el que esta leyendo (para interrumpirlo al cerrar)
};

// Las fuentes multimedia y de captura de los elementos de las escenas (hilo de interfaz). Dos
// elementos con la misma fuente la comparten (como una fuente de OBS en dos escenas).
class MediaSourceManager
{
public:
    MediaSourceManager (AuxBus& bus, juce::AudioFormatManager& formats, int maxWidth);
    ~MediaSourceManager();

    std::shared_ptr<FrameSource> get (const SceneItem& item);
    std::shared_ptr<FrameSource> find (int itemId) const;
    void keepOnly (const std::set<int>& itemIds);

    // Los elementos que se ven en la escena al aire; el resto, "inactivos".
    void setOnAir (const std::set<int>& itemIds);
    void setViewing (bool viewing);
    void tick();

private:
    AuxBus& bus;
    juce::AudioFormatManager& formats;
    const int maxWidth;
    juce::TimeSliceThread readAhead { "MiDJ multimedia" };
    std::map<int, std::shared_ptr<FrameSource>> sources;
    bool viewing = false;
};
}
