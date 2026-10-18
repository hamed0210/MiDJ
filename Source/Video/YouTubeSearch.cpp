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

    // Recorre el arbol buscando "videoRenderer" (los resultados normales).
    void collect (const juce::var& v, std::vector<YouTubeVideo>& out, int depth)
    {
        if (depth > 40)
            return;
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
                else
                    collect (p.value, out, depth + 1);
            }
        }
        else if (auto* array = v.getArray())
        {
            for (const auto& item : *array)
                collect (item, out, depth + 1);
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

std::vector<YouTubeVideo> YouTubeSearch::parseResultsPage (const juce::String& html)
{
    std::vector<YouTubeVideo> out;
    // var ytInitialData = {...};</script>
    auto start = html.indexOf ("ytInitialData");
    if (start < 0)
        return out;
    start = html.indexOfChar (start, '{');
    const auto end = html.indexOf (start, ";</script>");
    if (start < 0 || end < 0)
        return out;
    const auto data = juce::JSON::parse (html.substring (start, end));
    collect (data, out, 0);
    return out;
}

std::vector<YouTubeVideo> YouTubeSearch::search (const juce::String& query, juce::String& error)
{
    const auto url = juce::URL ("https://www.youtube.com/results").withParameter ("search_query", query).withParameter ("hl", "es");
    int status = 0;
    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs (15000)
                       .withStatusCode (&status)
                       .withExtraHeaders ("Accept-Language: es-419,es;q=0.9\r\n"
                                          "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
                                          "Chrome/131.0.0.0 Safari/537.36\r\n");
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
    auto videos = parseResultsPage (html);
    if (videos.empty())
        error = html.contains ("ytInitialData") ? "Sin resultados" : "YouTube cambio su pagina (no se encontraron los datos)";
    return videos;
}
}
