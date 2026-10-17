#pragma once

#include "Engine/Mixer.h"
#include "Video/Compositor.h"
#include "Video/DeckVideo.h"

namespace midj
{
// La mezcla de video de los dos decks segun el crossfader (como el crossfader de video de
// VirtualDJ, que por defecto sigue al de audio): da las capas de cada fotograma al compositor.
// Una cancion sin video se ve como un rotulo con el titulo y el artista; sin nada, el logo.
class VideoMix
{
public:
    VideoMix (std::array<DeckVideo*, 2> videos, std::array<const Deck*, 2> decks, const Mixer& mixer);

    // Peso de cada deck en la imagen: el crossfader, y si un deck esta vacio la imagen es del otro.
    static std::array<float, 2> weights (float crossfader, bool aHasPicture, bool bHasPicture);

    // Hilo de interfaz: el rotulo de un deck ("Artista - Titulo"; "" = deck vacio). Solo se
    // vuelve a dibujar si cambia el texto o el BPM.
    void setTitle (int deckIndex, const juce::String& title);

    // Hilo del compositor: las capas del fotograma (deck A debajo, B encima con su parte).
    void buildLayers (std::vector<Compositor::Layer>& layers);

    // Hilo del compositor: la imagen de un deck (su video o, si no tiene, su rotulo); no valida
    // si el deck esta vacio. Para las escenas, que la colocan donde diga el elemento.
    Compositor::Layer picture (int deckIndex);
    Compositor::Layer logoLayer() const;
    // Una imagen de un deck: en la CPU (image) o en la tarjeta grafica (gpu).
    static bool hasPicture (const Compositor::Layer& layer);
    static juce::Point<int> pictureSize (const Compositor::Layer& layer);
    float getCrossfader() const { return mixer.getCrossfader(); }
    // Nivel de video de cada deck (los faders de la pestana Video, como en VirtualDJ): 0 = no se ve.
    void setLevel (int deckIndex, float level) { levels[(size_t) deckIndex] = juce::jlimit (0.0f, 1.0f, level); }
    float getLevel (int deckIndex) const { return levels[(size_t) deckIndex].load(); }

    // El rotulo de una cancion sin video, del tamano del programa.
    static juce::Image renderCard (const juce::String& title, double bpm, int deckIndex, int width, int height);
    static juce::Image renderLogo (int width, int height);

    void setProgramSize (int width, int height);   // antes de empezar: tamano de rotulos y logo

private:
    struct Card
    {
        juce::String title;
        double bpm = 0.0;
        juce::Image image;
        juce::uint64 version = 0;
    };

    std::array<DeckVideo*, 2> videos;
    std::array<const Deck*, 2> decks;
    const Mixer& mixer;
    int width = 1280, height = 720;
    juce::SpinLock cardLock;
    std::array<Card, 2> cards;
    juce::Image logo;
    std::array<std::atomic<float>, 2> levels { 1.0f, 1.0f };
};
}
