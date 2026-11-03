#include "Remote/RemoteServer.h"

#include <deque>

namespace midj
{
namespace
{
    // SHA-1 (solo para la respuesta del saludo del WebSocket, RFC 6455; JUCE no la trae).
    std::array<juce::uint8, 20> sha1 (const void* data, size_t size)
    {
        juce::uint32 h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
        std::vector<juce::uint8> m ((const juce::uint8*) data, (const juce::uint8*) data + size);
        const juce::uint64 bits = (juce::uint64) size * 8;
        m.push_back (0x80);
        while (m.size() % 64 != 56)
            m.push_back (0);
        for (int i = 7; i >= 0; --i)
            m.push_back ((juce::uint8) (bits >> (i * 8)));
        auto rol = [] (juce::uint32 v, int n) { return (v << n) | (v >> (32 - n)); };
        for (size_t chunk = 0; chunk < m.size(); chunk += 64)
        {
            juce::uint32 w[80];
            for (int i = 0; i < 16; ++i)
                w[i] = (juce::uint32) m[chunk + (size_t) i * 4] << 24 | (juce::uint32) m[chunk + (size_t) i * 4 + 1] << 16
                     | (juce::uint32) m[chunk + (size_t) i * 4 + 2] << 8 | (juce::uint32) m[chunk + (size_t) i * 4 + 3];
            for (int i = 16; i < 80; ++i)
                w[i] = rol (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            juce::uint32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
            for (int i = 0; i < 80; ++i)
            {
                juce::uint32 f, k;
                if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
                else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
                else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
                else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
                const juce::uint32 t = rol (a, 5) + f + e + k + w[i];
                e = d; d = c; c = rol (b, 30); b = a; a = t;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
        }
        std::array<juce::uint8, 20> out {};
        for (int i = 0; i < 20; ++i)
            out[(size_t) i] = (juce::uint8) (h[i / 4] >> (24 - (i % 4) * 8));
        return out;
    }

    juce::MemoryBlock frame (int opcode, const void* data, size_t size)
    {
        juce::MemoryOutputStream out;
        out.writeByte ((char) (0x80 | opcode));
        if (size < 126)
            out.writeByte ((char) size);
        else if (size < 65536)
        {
            out.writeByte ((char) 126);
            out.writeShortBigEndian ((short) size);
        }
        else
        {
            out.writeByte ((char) 127);
            out.writeInt64BigEndian ((juce::int64) size);
        }
        if (size > 0)
            out.write (data, size);
        return out.getMemoryBlock();
    }

    bool writeAll (juce::StreamingSocket& socket, const void* data, size_t size)
    {
        auto* p = (const char*) data;
        while (size > 0)
        {
            const int n = socket.write (p, (int) juce::jmin (size, (size_t) 65536));
            if (n <= 0)
                return false;
            p += n;
            size -= (size_t) n;
        }
        return true;
    }

    constexpr int maxClients = 8;
    constexpr size_t maxMessage = 64 * 1024;
}

//==============================================================================
// Un celular: primero la peticion HTTP (la pagina o el saludo del WebSocket), luego sus mensajes.
class RemoteServer::Client : public juce::Thread
{
public:
    Client (RemoteServer& o, int clientId, std::unique_ptr<juce::StreamingSocket> s, juce::String k)
        : juce::Thread ("MiDJ control remoto"), owner (o), id (clientId), socket (std::move (s)), key (std::move (k)) {}

    ~Client() override
    {
        signalThreadShouldExit();
        socket->close();
        stopThread (2000);
    }

    void queue (juce::MemoryBlock data)
    {
        const std::lock_guard<std::mutex> l (outLock);
        if (outgoing.size() < 64)   // un celular que no lee: se tiran los mas viejos antes que llenar la memoria
            outgoing.push_back (std::move (data));
    }

    bool isOpen() const { return open.load(); }
    bool isFinished() const { return finished.load(); }
    const juce::String& getKey() const { return key; }

private:
    void run() override
    {
        serve();
        const bool wasOpen = open.exchange (false);
        socket->close();
        finished = true;
        if (wasOpen && owner.onClientsChanged)
            owner.onClientsChanged();
    }

    void serve()
    {
        // Peticion HTTP: hasta la linea en blanco (como mucho 8 KB y 5 s).
        juce::MemoryBlock in;
        const auto deadline = juce::Time::getMillisecondCounter() + 5000;
        int headerEnd = -1;
        while (headerEnd < 0)
        {
            if (threadShouldExit() || juce::Time::getMillisecondCounter() > deadline || in.getSize() > 8192)
                return;
            if (socket->waitUntilReady (true, 100) <= 0)
                continue;
            char buf[2048];
            const int n = socket->read (buf, sizeof (buf), false);
            if (n <= 0)
                return;
            in.append (buf, (size_t) n);
            headerEnd = in.toString().indexOf ("\r\n\r\n");
        }
        const auto head = in.toString().substring (0, headerEnd);
        juce::StringArray lines;
        lines.addTokens (head, "\r\n", {});
        lines.removeEmptyStrings();
        juce::StringArray first;
        first.addTokens (lines[0], " ", {});
        const auto target = first[1];
        const auto path = target.upToFirstOccurrenceOf ("?", false, false);
        juce::StringPairArray headers;
        for (int i = 1; i < lines.size(); ++i)
            headers.set (lines[i].upToFirstOccurrenceOf (":", false, false).trim().toLowerCase(),
                         lines[i].fromFirstOccurrenceOf (":", false, false).trim());

        if (first[0] != "GET")
            return respond ("405 Method Not Allowed", "text/plain", "Solo GET");
        if (path == "/ws")
        {
            if (! headers["upgrade"].containsIgnoreCase ("websocket") || headers["sec-websocket-key"].isEmpty())
                return respond ("400 Bad Request", "text/plain", "Falta el saludo del WebSocket");
            if (key.isEmpty() || queryValue (target, "k") != key)
                return respond ("403 Forbidden", "text/plain", "Clave incorrecta: vuelve a escanear el codigo QR de MiDJ");
            const juce::String reply = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                       "Sec-WebSocket-Accept: " + acceptKey (headers["sec-websocket-key"]) + "\r\n\r\n";
            if (! writeAll (*socket, reply.toRawUTF8(), reply.getNumBytesAsUTF8()))
                return;
            in.removeSection (0, (size_t) headerEnd + 4);
            open = true;
            if (owner.onClientsChanged)
                owner.onClientsChanged();
            return talk (in);
        }
        if (path == "/" || path == "/index.html")
        {
            juce::String html;
            {
                const std::lock_guard<std::mutex> l (owner.pageLock);
                html = owner.page;
            }
            return respond ("200 OK", "text/html; charset=utf-8", html);
        }
        if (path == "/favicon.ico")
            return respond ("204 No Content", "text/plain", {});
        respond ("404 Not Found", "text/plain", "No existe");
    }

    void respond (const juce::String& status, const juce::String& type, const juce::String& body)
    {
        const auto bytes = body.toRawUTF8();
        const auto size = body.getNumBytesAsUTF8();
        const juce::String head = "HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + juce::String ((juce::int64) size)
                                + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        if (writeAll (*socket, head.toRawUTF8(), head.getNumBytesAsUTF8()) && size > 0)
            writeAll (*socket, bytes, size);
    }

    // WebSocket abierto: leer tramas, contestar pings y escribir la cola. Cada 15 s un ping; si
    // el celular no da senales en 40 s (se apago la pantalla, salio del Wi-Fi) se cierra.
    void talk (juce::MemoryBlock in)
    {
        auto lastHeard = juce::Time::getMillisecondCounter(), lastPing = lastHeard;
        juce::MemoryBlock message;   // un mensaje partido en varias tramas
        while (! threadShouldExit())
        {
            const int ready = socket->waitUntilReady (true, 20);
            if (ready < 0)
                return;
            if (ready > 0)
            {
                char buf[4096];
                const int n = socket->read (buf, sizeof (buf), false);
                if (n <= 0)
                    return;
                in.append (buf, (size_t) n);
                lastHeard = juce::Time::getMillisecondCounter();
            }
            for (;;)
            {
                Frame f;
                const int got = takeFrame (in, f);
                if (got < 0)
                    return;
                if (got == 0)
                    break;
                if (f.opcode == 8)   // cerrar: se contesta y se acaba
                {
                    const auto close = frame (8, f.payload.getData(), juce::jmin ((size_t) 2, f.payload.getSize()));
                    writeAll (*socket, close.getData(), close.getSize());
                    return;
                }
                if (f.opcode == 9)
                {
                    queue (frame (10, f.payload.getData(), f.payload.getSize()));
                    continue;
                }
                if (f.opcode == 1 || f.opcode == 0)
                {
                    message.append (f.payload.getData(), f.payload.getSize());
                    if (message.getSize() > maxMessage)
                        return;
                    if (f.fin)
                    {
                        const auto parsed = juce::JSON::parse (message.toString());
                        message.reset();
                        if (parsed.isObject() && owner.onMessage)
                            owner.onMessage (id, parsed);
                    }
                }
            }
            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastHeard > 40000)
                return;
            if (now - lastPing > 15000)
            {
                lastPing = now;
                queue (frame (9, nullptr, 0));
            }
            std::deque<juce::MemoryBlock> out;
            {
                const std::lock_guard<std::mutex> l (outLock);
                out.swap (outgoing);
            }
            for (auto& block : out)
                if (! writeAll (*socket, block.getData(), block.getSize()))
                    return;
        }
    }

    RemoteServer& owner;
    const int id;
    std::unique_ptr<juce::StreamingSocket> socket;
    const juce::String key;
    std::atomic<bool> open { false }, finished { false };
    std::mutex outLock;
    std::deque<juce::MemoryBlock> outgoing;
};

//==============================================================================
RemoteServer::RemoteServer() : juce::Thread ("MiDJ control remoto (escucha)") {}

RemoteServer::~RemoteServer() { stop(); }

bool RemoteServer::start (int firstPort, const juce::String& newKey)
{
    stop();
    {
        const std::lock_guard<std::mutex> l (lock);
        key = newKey;
    }
    for (int p = firstPort; p < firstPort + 10; ++p)
    {
        auto socket = std::make_unique<juce::StreamingSocket>();
        if (socket->createListener (p))
        {
            listener = std::move (socket);
            port = p;
            startThread();
            return true;
        }
    }
    return false;
}

void RemoteServer::stop()
{
    signalThreadShouldExit();
    if (listener != nullptr)
        listener->close();
    stopThread (3000);
    listener.reset();
    port = 0;
    std::map<int, std::unique_ptr<Client>> old;
    {
        const std::lock_guard<std::mutex> l (lock);
        old.swap (clients);
    }
    old.clear();   // cada uno cierra su conexion y espera a su hilo
}

void RemoteServer::setKey (const juce::String& newKey)
{
    std::vector<std::unique_ptr<Client>> dropped;
    {
        const std::lock_guard<std::mutex> l (lock);
        key = newKey;
        for (auto it = clients.begin(); it != clients.end();)
            if (it->second->getKey() != newKey)
            {
                dropped.push_back (std::move (it->second));
                it = clients.erase (it);
            }
            else
                ++it;
    }
    dropped.clear();
}

void RemoteServer::run()
{
    while (! threadShouldExit())
    {
        removeFinished();
        if (listener->waitUntilReady (true, 200) <= 0)
            continue;
        std::unique_ptr<juce::StreamingSocket> socket (listener->waitForNextConnection());
        if (socket == nullptr || threadShouldExit())
            continue;
        const std::lock_guard<std::mutex> l (lock);
        if ((int) clients.size() >= maxClients * 2)
            continue;   // demasiadas conexiones a la vez: se cierra esta
        auto client = std::make_unique<Client> (*this, nextId, std::move (socket), key);
        client->startThread();
        clients[nextId++] = std::move (client);
    }
}

void RemoteServer::removeFinished()
{
    std::vector<std::unique_ptr<Client>> done;
    {
        const std::lock_guard<std::mutex> l (lock);
        for (auto it = clients.begin(); it != clients.end();)
            if (it->second->isFinished())
            {
                done.push_back (std::move (it->second));
                it = clients.erase (it);
            }
            else
                ++it;
    }
    done.clear();
}

void RemoteServer::broadcast (const juce::String& text)
{
    const auto data = textFrame (text);
    const std::lock_guard<std::mutex> l (lock);
    for (auto& [id, client] : clients)
        if (client->isOpen())
            client->queue (data);
}

void RemoteServer::send (int clientId, const juce::String& text)
{
    const std::lock_guard<std::mutex> l (lock);
    if (const auto it = clients.find (clientId); it != clients.end() && it->second->isOpen())
        it->second->queue (textFrame (text));
}

int RemoteServer::getNumClients() const
{
    const std::lock_guard<std::mutex> l (lock);
    int n = 0;
    for (auto& [id, client] : clients)
        n += client->isOpen() ? 1 : 0;
    return n;
}

juce::StringArray RemoteServer::localAddresses()
{
    juce::Array<juce::IPAddress> all;
    juce::IPAddress::findAllAddresses (all, false);
    juce::StringArray home, other;
    for (const auto& a : all)
    {
        const auto s = a.toString();
        const int b0 = a.address[0], b1 = a.address[1];
        if (b0 == 192 && b1 == 168)
            home.addIfNotAlreadyThere (s);
        else if (b0 == 10 || (b0 == 172 && b1 >= 16 && b1 <= 31))
            other.addIfNotAlreadyThere (s);
    }
    home.addArray (other);
    return home;
}

juce::String RemoteServer::randomKey()
{
    // Sin letras que se confunden (0/O, 1/l/I) por si alguien la escribe a mano.
    const juce::String chars ("abcdefghijkmnpqrstuvwxyz23456789");
    juce::Random random (juce::Time::getHighResolutionTicks() ^ (juce::int64) juce::Time::currentTimeMillis());
    juce::String k;
    for (int i = 0; i < 10; ++i)
        k += chars[random.nextInt (chars.length())];
    return k;
}

juce::String RemoteServer::acceptKey (const juce::String& clientKey)
{
    const auto text = clientKey.trim() + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const auto digest = sha1 (text.toRawUTF8(), text.getNumBytesAsUTF8());
    return juce::Base64::toBase64 (digest.data(), digest.size());
}

juce::MemoryBlock RemoteServer::textFrame (const juce::String& text)
{
    return frame (1, text.toRawUTF8(), text.getNumBytesAsUTF8());
}

int RemoteServer::takeFrame (juce::MemoryBlock& buffer, Frame& out)
{
    const auto* b = (const juce::uint8*) buffer.getData();
    const size_t size = buffer.getSize();
    if (size < 2)
        return 0;
    const bool masked = (b[1] & 0x80) != 0;
    juce::uint64 length = b[1] & 0x7f;
    size_t pos = 2;
    if (length == 126)
    {
        if (size < 4)
            return 0;
        length = (juce::uint64) b[2] << 8 | b[3];
        pos = 4;
    }
    else if (length == 127)
    {
        if (size < 10)
            return 0;
        length = 0;
        for (int i = 0; i < 8; ++i)
            length = length << 8 | b[2 + i];
        pos = 10;
    }
    if (length > maxMessage || (b[0] & 0x70) != 0)
        return -1;   // demasiado grande o con extensiones que no se pidieron
    const size_t maskPos = pos;
    if (masked)
        pos += 4;
    if (size < pos + length)
        return 0;
    out.fin = (b[0] & 0x80) != 0;
    out.opcode = b[0] & 0x0f;
    out.payload.replaceAll (b + pos, (size_t) length);
    if (masked)
    {
        auto* p = (juce::uint8*) out.payload.getData();
        for (size_t i = 0; i < length; ++i)
            p[i] ^= b[maskPos + (i & 3)];
    }
    buffer.removeSection (0, pos + (size_t) length);
    return 1;
}

juce::String RemoteServer::queryValue (const juce::String& target, const juce::String& name)
{
    juce::StringArray pairs;
    pairs.addTokens (target.fromFirstOccurrenceOf ("?", false, false), "&", {});
    for (const auto& pair : pairs)
        if (pair.upToFirstOccurrenceOf ("=", false, false) == name)
            return juce::URL::removeEscapeChars (pair.fromFirstOccurrenceOf ("=", false, false));
    return {};
}
}
