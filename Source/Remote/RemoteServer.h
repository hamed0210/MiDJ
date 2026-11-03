#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace midj
{
// Control remoto desde el celular por la red local (Wi-Fi), sin internet ni servidores: MiDJ
// sirve la pagina del control (GET /) y habla con ella por WebSocket (GET /ws?k=<clave>). El
// codigo QR lleva la direccion con la clave; sin la clave la pagina abre pero no controla nada.
//
// Un hilo acepta conexiones y cada celular tiene el suyo: lee sus mensajes y le escribe lo que
// haya en su cola, asi un celular lento nunca frena al que llama a broadcast() (la interfaz).
// Los mensajes son texto JSON en los dos sentidos (ver el protocolo en MainComponent).
class RemoteServer : private juce::Thread
{
public:
    RemoteServer();
    ~RemoteServer() override;

    // Escucha en el primer puerto libre desde firstPort (prueba 10). false si ninguno abre.
    bool start (int firstPort, const juce::String& key);
    void stop();
    bool isRunning() const { return port > 0; }
    int getPort() const { return port; }
    void setKey (const juce::String& key);   // los conectados con la clave anterior se desconectan
    void setPage (const juce::String& html) { const std::lock_guard<std::mutex> l (pageLock); page = html; }

    // Un mensaje de un celular (hilo de ese celular): su id y el JSON ya leido.
    std::function<void (int clientId, const juce::var& message)> onMessage;
    // Se conecto o se fue un celular (hilo de ese celular).
    std::function<void()> onClientsChanged;

    void broadcast (const juce::String& text);          // a todos (cualquier hilo, no bloquea)
    void send (int clientId, const juce::String& text); // a uno
    int getNumClients() const;

    // Las direcciones de este equipo en la red local (192.168..., 10..., 172.16-31...), la de la
    // red de casa primero; para el QR: http://<ip>:<puerto>/?k=<clave>.
    static juce::StringArray localAddresses();
    static juce::String randomKey();

    // Partes del protocolo (publicas para las pruebas).
    static juce::String acceptKey (const juce::String& clientKey);   // Sec-WebSocket-Accept
    static juce::MemoryBlock textFrame (const juce::String& text);   // servidor -> celular (sin mascara)
    struct Frame { int opcode = 0; bool fin = true; juce::MemoryBlock payload; };
    // Saca una trama completa del principio de 'buffer' (la quita). 0 = falta, -1 = invalida, 1 = ok.
    static int takeFrame (juce::MemoryBlock& buffer, Frame& frame);
    static juce::String queryValue (const juce::String& target, const juce::String& name);   // "/ws?k=abc" -> "abc"

private:
    class Client;
    void run() override;   // aceptar conexiones
    void removeFinished();

    std::unique_ptr<juce::StreamingSocket> listener;
    std::atomic<int> port { 0 };
    juce::String key;
    mutable std::mutex lock;   // clientes y clave
    std::map<int, std::unique_ptr<Client>> clients;
    int nextId = 1;
    std::mutex pageLock;
    juce::String page;
};
}
