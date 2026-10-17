#pragma once

#include "Engine/Mixer.h"
#include "Video/DeckVideo.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// La salida de video: la mezcla de los dos decks segun el crossfader (como el crossfader de
// video de VirtualDJ, que por defecto sigue al de audio). Una cancion sin video se ve como un
// rotulo con el titulo y el artista. Doble clic: pantalla completa (en el segundo monitor si hay).
class VideoOutput : public juce::Component, private juce::Timer
{
public:
    VideoOutput (std::array<DeckVideo*, 2> videos, std::array<const Deck*, 2> decks, const Mixer& mixer);

    // Titulo que se rotula para la cancion de un deck sin video ("" = deck vacio).
    std::function<juce::String (int deckIndex)> getTitle;
    std::function<void()> onDoubleClick;

    // Peso de cada deck en la imagen: el crossfader, y si un deck esta vacio la imagen es del otro.
    static std::array<float, 2> weights (float crossfader, bool aHasPicture, bool bHasPicture);

    void paint (juce::Graphics& g) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void drawSource (juce::Graphics& g, int deckIndex, float alpha);

    std::array<DeckVideo*, 2> videos;
    std::array<const Deck*, 2> decks;
    const Mixer& mixer;
    std::array<double, 2> shownTimes { -2.0, -2.0 };
    float shownCrossfader = -1.0f;
    std::array<juce::String, 2> shownTitles;
};

// Ventana aparte con la salida de video.
class VideoWindow : public juce::DocumentWindow
{
public:
    VideoWindow (std::array<DeckVideo*, 2> videos, std::array<const Deck*, 2> decks, const Mixer& mixer);

    VideoOutput& getOutput() { return *output; }
    void toggleFullScreen();   // pantalla completa sin barra ni borde (Esc o doble clic para salir)
    bool isShowingFullScreen() const { return fullScreenMode; }
    std::function<void()> onClosed;

    void closeButtonPressed() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    VideoOutput* output = nullptr;   // lo posee la ventana (setContentOwned)
    juce::Rectangle<int> windowedBounds;
    bool fullScreenMode = false;
};
}
