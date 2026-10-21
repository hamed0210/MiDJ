#pragma once

#include "UI/Icons.h"
#include "UI/YouTubePlayer.h"
#include "UI/YouTubeThumbnails.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace midj
{
// La lista del automix de YouTube (separada de la de los decks): los videos en orden y si esta
// encendido. Se guarda en youtube.xml.
class YouTubeQueue : public juce::ChangeBroadcaster
{
public:
    void add (const YouTubeVideo& v)        { items.push_back (v); sendChangeMessage(); }
    void insertNext (const YouTubeVideo& v) { items.insert (items.begin(), v); sendChangeMessage(); }
    void remove (int index);
    void clear()                            { items.clear(); sendChangeMessage(); }
    void setEnabled (bool on)               { enabled = on; sendChangeMessage(); }
    bool isEnabled() const                  { return enabled; }
    const std::vector<YouTubeVideo>& getItems() const { return items; }
    // Saca el primero (el siguiente que suena). false si no hay.
    bool takeNext (YouTubeVideo& out);

    void save (const juce::File& file) const;
    void load (const juce::File& file);

    // Ajustes del reproductor que se guardan con la lista: calidad y saltar anuncios.
    juce::String quality { "hd720" };
    bool skipAds = false;

private:
    std::vector<YouTubeVideo> items;
    bool enabled = false;
};

// Pestana YouTube del panel del automix: lo que suena (titulo, estado, avance con el tiempo,
// Play/Pausa, Detener y volumen) y la lista del automix de YouTube (siguiente, quitar, vaciar).
// Al terminar un video: con el automix encendido suena el siguiente de la lista; si no, el
// reproductor se cierra (libera su memoria).
class YouTubePanel : public juce::Component,
                     private juce::ChangeListener,
                     private juce::ListBoxModel
{
public:
    YouTubePanel (YouTubePlayer& player, YouTubeQueue& queue);
    ~YouTubePanel() override;

    void playNow (const YouTubeVideo& video);   // desde la biblioteca
    void next();                                // el siguiente de la lista (o cerrar si no hay)
    void setAutomix (bool on);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;   // clic en la barra de avance: saltar ahi

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    int getNumRows() override { return (int) queue.getItems().size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void deleteKeyPressed (int row) override { queue.remove (row); }
    void updateButtons();
    juce::String getTooltipForRow (int row) override;
    void showSettingsMenu();

    YouTubePlayer& player;
    YouTubeQueue& queue;
    juce::ListBox list;
    IconButton playButton { Icons::Glyph::play, "Play / Pausa del video de YouTube" },
               stopButton { Icons::Glyph::stop, "Detener y cerrar el reproductor" },
               nextButton { Icons::Glyph::skip, "Siguiente video de la lista" },
               clearButton { Icons::Glyph::trash, "Vaciar la lista de YouTube" };
    juce::Slider volume;
    juce::TextButton settingsButton;   // "720p": calidad y saltar anuncios
    juce::SharedResourcePointer<YouTubeThumbnails> thumbnails;
    juce::Rectangle<int> nowArea, progressArea, timeArea;
    bool errorPending = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YouTubePanel)
};
}
