#include "Video/YouTubeSearch.h"

#include <algorithm>

namespace midj
{
namespace
{
    // El texto de un campo de YouTube: {"simpleText": "..."} o {"runs": [{"text": "..."}, ...]}.
    juce::String textOf (const juce::var& v)
    {
        if (v.hasProperty ("simpleText"))
            return v["simpleText"].toString();
        juce::String s;
        if (auto* runs = v["runs"].getArray())
            for (const auto& r : *runs)
                s << r["text"].toString();
        return s;
    }

    bool isVideoId (const juce::String& s)
    {
        return s.length() == 11 && s.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-");
    }

    const char* const userAgent = "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
                                  "Chrome/131.0.0.0 Safari/537.36\r\n";

    // Recorre el arbol buscando "videoRenderer" (los resultados normales) y la continuacion de la
    // lista ("continuationItemRenderer", al final; las de los filtros de arriba son otras).
    void collect (const juce::var& v, YouTubeSearch::Page& page, int depth)
    {
        if (depth > 40)
            return;
        auto& out = page.videos;
        if (auto* object = v.getDynamicObject())
        {
            for (const auto& p : object->getProperties())
            {
                if (p.name == juce::Identifier ("videoRenderer"))
                {
                    const auto& r = p.value;
                    YouTubeVideo video;
                    video.id = r["videoId"].toString();
                    video.title = textOf (r["title"]);
                    video.channel = textOf (r["ownerText"]);
                    if (video.channel.isEmpty())
                        video.channel = textOf (r["longBylineText"]);
                    video.seconds = YouTubeSearch::parseDuration (textOf (r["lengthText"]));
                    // Sin duracion: directo o estreno (no se pueden mezclar como una cancion).
                    if (isVideoId (video.id) && video.title.isNotEmpty() && video.seconds > 0.0
                        && std::none_of (out.begin(), out.end(), [&] (const YouTubeVideo& o) { return o.id == video.id; }))
                        out.push_back (video);
                }
                else if (p.name == juce::Identifier ("continuationItemRenderer"))
                {
                    if (page.continuation.isEmpty())
                        page.continuation = p.value["continuationEndpoint"]["continuationCommand"]["token"].toString();
                }
                else
                    collect (p.value, page, depth + 1);
            }
        }
        else if (auto* array = v.getArray())
        {
            for (const auto& item : *array)
                collect (item, page, depth + 1);
        }
    }
}

double YouTubeSearch::parseDuration (const juce::String& text)
{
    juce::StringArray parts;
    parts.addTokens (text.trim(), ":", "");
    if (parts.isEmpty() || parts.size() > 3)
        return 0.0;
    double seconds = 0.0;
    for (const auto& p : parts)
    {
        if (p.isEmpty() || ! p.containsOnly ("0123456789"))
            return 0.0;
        seconds = seconds * 60.0 + p.getIntValue();
    }
    return seconds;
}

juce::String YouTubeSearch::formatDuration (double seconds)
{
    const int total = juce::roundToInt (seconds);
    if (total <= 0)
        return {};
    if (total >= 3600)
        return juce::String::formatted ("%d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);
    return juce::String::formatted ("%d:%02d", total / 60, total % 60);
}

juce::String YouTubeSearch::idFromText (const juce::String& raw)
{
    const auto text = raw.trim();
    if (isVideoId (text))
        return text;
    // youtube.com/watch?v=ID, youtu.be/ID, youtube.com/shorts/ID, youtube.com/embed/ID
    for (const auto* marker : { "v=", "youtu.be/", "/shorts/", "/embed/", "/live/" })
        if (text.contains (marker))
        {
            const auto id = text.fromFirstOccurrenceOf (marker, false, false).substring (0, 11);
            if (isVideoId (id))
                return id;
        }
    return {};
}

YouTubeSearch::Page YouTubeSearch::parseResultsPage (const juce::String& html)
{
    Page page;
    // var ytInitialData = {...};</script>
    auto start = html.indexOf ("ytInitialData");
    if (start < 0)
        return page;
    start = html.indexOfChar (start, '{');
    const auto end = html.indexOf (start, ";</script>");
    if (start < 0 || end < 0)
        return page;
    const auto data = juce::JSON::parse (html.substring (start, end));
    collect (data, page, 0);
    // "INNERTUBE_CLIENT_VERSION":"2.2026..." (para pedir los siguientes)
    const auto version = html.fromFirstOccurrenceOf ("\"INNERTUBE_CLIENT_VERSION\":\"", false, false).upToFirstOccurrenceOf ("\"", false, false);
    page.clientVersion = version.length() < 40 ? version : juce::String();
    return page;
}

YouTubeSearch::Page YouTubeSearch::parseMoreResults (const juce::String& json)
{
    Page page;
    collect (juce::JSON::parse (json), page, 0);
    return page;
}

YouTubeSearch::Page YouTubeSearch::search (const juce::String& query, juce::String& error)
{
    const auto url = juce::URL ("https://www.youtube.com/results").withParameter ("search_query", query).withParameter ("hl", "es");
    int status = 0;
    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs (15000)
                       .withStatusCode (&status)
                       .withExtraHeaders (juce::String ("Accept-Language: es-419,es;q=0.9\r\n") + userAgent);
    auto stream = url.createInputStream (options);
    if (stream == nullptr)
    {
        error = "Sin conexion con YouTube";
        return {};
    }
    const auto html = stream->readEntireStreamAsString();
    if (status != 0 && status != 200)
    {
        error = "YouTube respondio " + juce::String (status);
        return {};
    }
    auto page = parseResultsPage (html);
    if (page.videos.empty())
        error = html.contains ("ytInitialData") ? "Sin resultados" : "YouTube cambio su pagina (no se encontraron los datos)";
    return page;
}

YouTubeSearch::Page YouTubeSearch::searchMore (const juce::String& continuation, const juce::String& clientVersion, juce::String& error)
{
    auto* client = new juce::DynamicObject();
    client->setProperty ("clientName", "WEB");
    client->setProperty ("clientVersion", clientVersion);
    client->setProperty ("hl", "es");
    auto* context = new juce::DynamicObject();
    context->setProperty ("client", juce::var (client));
    auto* body = new juce::DynamicObject();
    body->setProperty ("context", juce::var (context));
    body->setProperty ("continuation", continuation);

    // Sin parametros en la direccion: con el POST, JUCE los pasaria al cuerpo y YouTube responde 400.
    const auto url = juce::URL ("https://www.youtube.com/youtubei/v1/search")
                         .withPOSTData (juce::JSON::toString (juce::var (body), true));
    int status = 0;
    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                       .withConnectionTimeoutMs (15000)
                       .withStatusCode (&status)
                       .withExtraHeaders (juce::String ("Content-Type: application/json\r\nAccept-Language: es-419,es;q=0.9\r\n") + userAgent);
    auto stream = url.createInputStream (options);
    if (stream == nullptr)
    {
        error = "Sin conexion con YouTube";
        return {};
    }
    const auto json = stream->readEntireStreamAsString();
    if (status != 0 && status != 200)
    {
        error = "YouTube respondio " + juce::String (status);
        return {};
    }
    auto page = parseMoreResults (json);
    page.clientVersion = clientVersion;
    return page;
}
}
