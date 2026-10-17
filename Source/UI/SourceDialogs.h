#pragma once

#include "UI/Icons.h"
#include "UI/VideoWindow.h"

namespace midj
{
// Dialogo Anadir fuente, como el de OBS: a la izquierda los tipos; a la derecha "Crear nueva
// fuente" o anadir una que ya existe en otra escena (la misma fuente: comparte captura o video).
class AddSourceWindow : public juce::DocumentWindow
{
public:
    explicit AddSourceWindow (VideoStage& stage);
    void closeButtonPressed() override;

private:
    class Content;
    VideoStage& stage;
};

// Propiedades para 'fuente', como en OBS: los ajustes de la fuente (se ven al momento),
// su colocacion, y Por defecto / Aceptar / Cancelar (Cancelar deja todo como estaba).
class SourcePropertiesWindow : public juce::DocumentWindow
{
public:
    SourcePropertiesWindow (VideoStage& stage, int itemId);
    void closeButtonPressed() override;
    int getItemId() const { return itemId; }

private:
    class Content;
    VideoStage& stage;
    const int itemId;
};
}
