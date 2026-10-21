#pragma once

#include "Video/YouTubeSearch.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace midj
{
// Reproductor de YouTube: una ventana fuera de las pantallas con WebView2 (el motor de Edge)
// que abre la pagina normal del video (asi se ven tambien los que no se dejan incrustar), le
// quita todo menos el video con un script y lo controla desde MiDJ. La fuente de escena YouTube
// captura esa ventana. La ventana solo existe mientras hay un video: close() la cierra y libera
// la memoria (unos 400 MB mientras esta abierta).
// El sonido sale por la tarjeta predeterminada de Windows (no pasa por el mezclador de MiDJ).
// El script se puede cambiar sin recompilar dejando un youtube.js en la carpeta de datos.
// Todo en el hilo de la interfaz.
class YouTubePlayer : public juce::ChangeBroadcaster
{
public:
    // verify: Google o YouTube piden demostrar que no es un robot (captcha o iniciar sesion).
    enum class State { closed, opening, ad, playing, paused, ended, error, verify };
    struct Status
    {
        State state = State::closed;
        double time = 0.0, duration = 0.0;
        juce::String error;
    };

    YouTubePlayer (const juce::File& dataFolder, int width, int height);
    ~YouTubePlayer() override;

    // Abre la ventana si hace falta y carga el video; empieza a sonar solo (tras los anuncios).
    void open (const YouTubeVideo& video);
    void play();
    void pause();
    void togglePlay();
    void seek (double seconds);
    void setVolume (float volume);   // 0..1
    float getVolume() const { return volume; }
    void close();

    // Calidad pedida a YouTube ("auto", "medium" 360p, "large" 480p, "hd720", "hd1080"; se
    // aplica con el video ya sonando) y saltar anuncios (Omitir en cuanto se puede; si no, el
    // anuncio sin sonido y al final). Valen para el video actual y los siguientes.
    void setQuality (const juce::String& quality);
    const juce::String& getQuality() const { return quality; }
    void setSkipAds (bool skip);
    bool getSkipAds() const { return skipAds; }
    // Saltar ya el anuncio que se ve (por si el salto automatico falla o esta apagado).
    void skipAd();

    // Verificacion (estado verify): la ventana del reproductor se pone a la vista para que el
    // usuario resuelva el captcha o inicie sesion; al volver a sonar el video, se esconde sola.
    void showVerification();
    void hideVerification();
    bool isVerifying() const;
    static juce::StringArray getQualities();
    static juce::String qualityName (const juce::String& quality);   // "720p", "Auto"...

    const Status& getStatus() const { return status; }
    const YouTubeVideo& getVideo() const { return video; }
    // Suena algo (el video o un anuncio): la escena automatica de YouTube.
    bool isSounding() const { return status.state == State::playing || status.state == State::ad; }
    bool isOpen() const { return status.state != State::closed; }

    std::function<void()> onEnded;   // el video termino (no un anuncio)

    static State stateFromText (const juce::String& s);
    static juce::String stateText (const Status& s);   // para el panel: "Sonando", "Anuncio"...

private:
    struct Impl;
    void handleMessage (const juce::String& json);
    void send (const juce::String& json);
    void sendSettings();
    void setState (State s);

    std::unique_ptr<Impl> impl;
    juce::File dataFolder;
    int width, height;
    YouTubeVideo video;
    Status status;
    float volume = 0.8f;
    juce::String quality { "hd720" };
    bool skipAds = true;
};
}
