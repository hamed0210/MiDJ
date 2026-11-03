#include "App/Updates.h"

namespace midj
{
juce::String Updates::releasesPage()
{
    return "https://github.com/" + juce::String (repository) + "/releases/latest";
}

int Updates::compareVersions (const juce::String& a, const juce::String& b)
{
    auto parts = [] (const juce::String& v)
    {
        juce::StringArray p;
        p.addTokens (v.trim().trimCharactersAtStart ("vV").upToFirstOccurrenceOf ("-", false, false), ".", {});
        return p;
    };
    const auto pa = parts (a), pb = parts (b);
    for (int i = 0; i < juce::jmax (pa.size(), pb.size()); ++i)
    {
        const int x = pa[i].getIntValue(), y = pb[i].getIntValue();   // la que falta cuenta como 0
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

bool Updates::parseLatestRelease (const juce::String& json, Release& out)
{
    const auto root = juce::JSON::parse (json);
    if (! root.isObject() || (bool) root["draft"] || (bool) root["prerelease"])
        return false;
    const auto tag = root["tag_name"].toString().trim();
    if (tag.isEmpty() || ! tag.trimCharactersAtStart ("vV").containsOnly ("0123456789.-abcdefghijklmnopqrstuvwxyz"))
        return false;
    out = {};
    out.version = tag.trimCharactersAtStart ("vV");
    out.name = root["name"].toString();
    out.notes = root["body"].toString();
    out.pageUrl = root["html_url"].toString();
    if (const auto* assets = root["assets"].getArray())
        for (const auto& asset : *assets)
        {
            const auto name = asset["name"].toString();
            if (name.startsWithIgnoreCase ("MiDJ-Setup") && name.endsWithIgnoreCase (".exe"))
            {
                out.installerUrl = asset["browser_download_url"].toString();
                out.installerSize = (juce::int64) asset["size"];
                break;
            }
        }
    return true;
}

juce::String Updates::plainNotes (const juce::String& markdown)
{
    juce::StringArray lines;
    lines.addLines (markdown.replace ("\r", ""));
    juce::StringArray result;
    for (auto line : lines)
    {
        line = line.trimEnd();
        const auto trimmed = line.trimStart();
        if (trimmed.startsWith ("#"))
            line = trimmed.trimCharactersAtStart ("# ");
        else if (trimmed.startsWith ("- ") || trimmed.startsWith ("* "))
            line = line.substring (0, line.length() - trimmed.length()) + juce::String::fromUTF8 ("\xc2\xb7 ") + trimmed.substring (2);
        line = line.replace ("**", "").replace ("`", "");
        if (line.isNotEmpty() || (! result.isEmpty() && result[result.size() - 1].isNotEmpty()))   // una sola linea en blanco seguida
            result.add (line);
    }
    return result.joinIntoString ("\n").trim();
}

bool Updates::fetchLatest (Release& out, juce::String& error)
{
    const juce::URL url ("https://api.github.com/repos/" + juce::String (repository) + "/releases/latest");
    int status = 0;
    auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                             .withConnectionTimeoutMs (15000)
                                             .withNumRedirectsToFollow (5)
                                             .withStatusCode (&status)
                                             .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: MiDJ\r\n"));
    if (stream == nullptr)
    {
        error = "Sin conexion con GitHub";
        return false;
    }
    const auto json = stream->readEntireStreamAsString();
    if (status == 404)
    {
        error = "Aun no hay versiones publicadas";
        return false;
    }
    if (status != 0 && status != 200)
    {
        error = "GitHub respondio " + juce::String (status) + (status == 403 ? " (demasiadas consultas: prueba en una hora)" : "");
        return false;
    }
    if (! parseLatestRelease (json, out))
    {
        error = "La respuesta de GitHub no trae una version";
        return false;
    }
    return true;
}

Updates::Settings Updates::Settings::load (const juce::File& file)
{
    Settings s;
    if (const auto xml = juce::XmlDocument::parse (file); xml != nullptr && xml->hasTagName ("UPDATES"))
    {
        s.checkOnStartup = xml->getBoolAttribute ("auto", true);
        s.skippedVersion = xml->getStringAttribute ("skipped");
        s.lastCheckMs = xml->getStringAttribute ("lastCheck").getLargeIntValue();
    }
    return s;
}

void Updates::Settings::save (const juce::File& file) const
{
    juce::XmlElement xml ("UPDATES");
    xml.setAttribute ("auto", checkOnStartup);
    xml.setAttribute ("skipped", skippedVersion);
    xml.setAttribute ("lastCheck", juce::String (lastCheckMs));
    file.getParentDirectory().createDirectory();
    xml.writeTo (file);
}
}
