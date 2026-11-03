#include "UI/RemotePanel.h"
#include "UI/DarkLookAndFeel.h"
#include "ThirdParty/qrcodegen/qrcodegen.hpp"

namespace midj
{
void QrCodeView::setText (const juce::String& text)
{
    modules.clear();
    size = 0;
    if (text.isNotEmpty())
    {
        try
        {
            const auto code = qrcodegen::QrCode::encodeText (text.toRawUTF8(), qrcodegen::QrCode::Ecc::MEDIUM);
            size = code.getSize();
            modules.resize ((size_t) (size * size));
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                    modules[(size_t) (y * size + x)] = code.getModule (x, y);
        }
        catch (const std::exception&)   // texto demasiado largo para un QR (no pasa con una direccion)
        {
            size = 0;
        }
    }
    repaint();
}

void QrCodeView::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    if (size == 0)
    {
        g.setColour (DarkLookAndFeel::raised);
        g.fillRoundedRectangle (area, 8.0f);
        return;
    }
    // Margen de 4 modulos (el que pide el estandar) (los lectores necesitan un borde claro) y modulos de pixeles enteros.
    const int total = size + 8;
    const float cell = std::floor (juce::jmin (area.getWidth(), area.getHeight()) / (float) total);
    const float side = cell * (float) total;
    const auto box = area.withSizeKeepingCentre (side, side);
    g.setColour (juce::Colours::white);
    g.fillRoundedRectangle (box, 6.0f);
    g.setColour (juce::Colours::black);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (modules[(size_t) (y * size + x)])
                g.fillRect (box.getX() + (float) (x + 4) * cell, box.getY() + (float) (y + 4) * cell, cell, cell);
}

//==============================================================================
RemotePanel::RemotePanel (RemoteServer& s, Hooks h) : server (s), hooks (std::move (h))
{
    enable.setToggleState (hooks.isEnabled(), juce::dontSendNotification);
    enable.onClick = [this] { hooks.setEnabled (enable.getToggleState()); refresh(); };
    addAndMakeVisible (enable);
    addAndMakeVisible (qr);
    address.setReadOnly (true);
    address.setJustification (juce::Justification::centred);
    address.setFont (DarkLookAndFeel::mono (13.0f));
    addAndMakeVisible (address);
    network.setTooltip ("La red por la que esta conectado el celular");
    network.onChange = [this] { refresh(); };
    addChildComponent (network);
    keyButton.setTooltip ("Una clave nueva: los celulares conectados se desconectan y hay que volver a escanear el codigo");
    keyButton.onClick = [this] { hooks.newKey(); refresh(); };
    addAndMakeVisible (keyButton);
    setSize (340, 560);
    refresh();
    startTimer (1000);
}

juce::String RemotePanel::url() const
{
    if (! server.isRunning() || addresses.isEmpty())
        return {};
    const auto ip = addresses[juce::jmax (0, network.getSelectedItemIndex())];
    return "http://" + ip + ":" + juce::String (server.getPort()) + "/?k=" + hooks.getKey();
}

void RemotePanel::refresh()
{
    const auto now = RemoteServer::localAddresses();
    if (now != addresses)
    {
        const auto previous = network.getText();
        addresses = now;
        network.clear (juce::dontSendNotification);
        for (int i = 0; i < addresses.size(); ++i)
            network.addItem (addresses[i], i + 1);
        const int keep = addresses.indexOf (previous);
        network.setSelectedItemIndex (keep >= 0 ? keep : 0, juce::dontSendNotification);
        network.setVisible (addresses.size() > 1);
        resized();
    }
    const auto link = url();
    if (link != shownUrl)
    {
        shownUrl = link;
        qr.setText (link);
        address.setText (link.upToFirstOccurrenceOf ("/?", false, false), false);
    }
    qr.setVisible (link.isNotEmpty());
    address.setVisible (link.isNotEmpty());
    keyButton.setEnabled (server.isRunning());
    const int n = server.getNumClients();
    if (! hooks.isEnabled())
        status = "Apagado. Enciendelo para controlar MiDJ desde el celular.";
    else if (! server.isRunning())
        status = "No se pudo abrir el puerto del control remoto (otro programa lo usa).";
    else if (addresses.isEmpty())
        status = "Este PC no esta conectado a ninguna red Wi-Fi o de cable.";
    else
        status = n == 0 ? "Esperando al celular..." : (n == 1 ? "1 celular conectado" : juce::String (n) + " celulares conectados");
    repaint();
}

void RemotePanel::paint (juce::Graphics& g)
{
    g.setColour (DarkLookAndFeel::text);
    g.setFont (DarkLookAndFeel::sans (16.0f, true));
    g.drawText ("Control remoto", 16, 12, getWidth() - 32, 24, juce::Justification::centredLeft);
    const bool connected = server.getNumClients() > 0;
    g.setColour (connected ? DarkLookAndFeel::good : DarkLookAndFeel::textQuiet);
    g.setFont (DarkLookAndFeel::sans (13.0f, connected));
    g.drawFittedText (status, 16, getHeight() - 132, getWidth() - 32, 36, juce::Justification::centred, 2);
    g.setColour (DarkLookAndFeel::textFaint);
    g.setFont (DarkLookAndFeel::sans (12.0f));
    g.drawFittedText ("Conecta el celular al mismo Wi-Fi que este PC y escanea el codigo con la camara. "
                      "Si Windows pregunta por el firewall, permite las redes privadas.",
                      16, getHeight() - 92, getWidth() - 32, 44, juce::Justification::centred, 3);
}

void RemotePanel::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (32);
    enable.setBounds (area.removeFromTop (30));
    area.removeFromTop (8);
    if (network.isVisible())
    {
        network.setBounds (area.removeFromTop (28));
        area.removeFromTop (8);
    }
    keyButton.setBounds (area.removeFromBottom (32).withSizeKeepingCentre (160, 32));
    area.removeFromBottom (96);   // estado y ayuda (paint)
    address.setBounds (area.removeFromBottom (28));
    area.removeFromBottom (8);
    qr.setBounds (area);
}
}
