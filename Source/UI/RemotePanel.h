#pragma once

#include "Remote/RemoteServer.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// Codigo QR de un texto (la direccion del control remoto), en negro sobre blanco con su margen.
class QrCodeView : public juce::Component
{
public:
    void setText (const juce::String& text);
    void paint (juce::Graphics& g) override;

private:
    int size = 0;
    std::vector<bool> modules;   // size x size, true = negro
};

// Ventanita del boton del celular (barra de arriba): encender el control remoto, el codigo QR
// para abrirlo en el celular, la direccion por si se escribe a mano, cuantos hay conectados y
// cambiar la clave (desconecta a todos).
class RemotePanel : public juce::Component, private juce::Timer
{
public:
    struct Hooks
    {
        std::function<bool()> isEnabled;
        std::function<void (bool)> setEnabled;
        std::function<juce::String()> getKey;
        std::function<void()> newKey;
    };
    RemotePanel (RemoteServer& server, Hooks hooks);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override { refresh(); }
    void refresh();
    juce::String url() const;

    RemoteServer& server;
    Hooks hooks;
    juce::ToggleButton enable { "Control remoto desde el celular" };
    QrCodeView qr;
    juce::TextEditor address;
    juce::ComboBox network;   // si el PC tiene varias redes (Wi-Fi y cable, VPN...)
    juce::TextButton keyButton { "Cambiar clave" };
    juce::String status, shownUrl;
    juce::StringArray addresses;
};
}
