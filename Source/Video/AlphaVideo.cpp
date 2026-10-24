#include "Video/AlphaVideo.h"
#include "Library/Library.h"

namespace midj
{
namespace
{
    juce::File dataFolder() { return Library::getDefaultFile().getParentDirectory(); }

    // Formatos de pixel con canal alfa (ffmpeg -pix_fmts).
    bool pixelFormatHasAlpha (const juce::String& line)
    {
        for (const auto* f : { "yuva", "rgba", "argb", "bgra", "abgr", "gbrap", "ya8", "ya16" })
            if (line.containsIgnoreCase (f))
                return true;
        return false;
    }

    // "00:00:02.03" -> 2.03
    double parseClock (const juce::String& text)
    {
        juce::StringArray parts;
        parts.addTokens (text.trim(), ":", "");
        if (parts.size() != 3)
            return 0.0;
        return parts[0].getIntValue() * 3600.0 + parts[1].getIntValue() * 60.0 + parts[2].getDoubleValue();
    }
}

// La compilacion GPL de BtbN en GitHub (con libx264 y libvpx): unos 200 MB, pero GitHub va rapido
// (gyan.dev, de 115 MB, bajaba a unos 60 KB/s: media hora).
const char* const AlphaVideo::ffmpegDownloadUrl = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip";

bool AlphaVideo::mightHaveAlpha (const juce::File& video)
{
    return video.hasFileExtension ("webm;mov;mkv");
}

juce::File AlphaVideo::downloadedFfmpeg()
{
    return dataFolder().getChildFile ("ffmpeg").getChildFile ("ffmpeg.exe");
}

juce::File AlphaVideo::findFfmpeg()
{
    juce::Array<juce::File> candidates { downloadedFfmpeg(),
                                         juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("ffmpeg.exe") };
    juce::StringArray path;
    path.addTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), ";", "\"");
    for (const auto& dir : path)
        if (dir.trim().isNotEmpty() && juce::File::isAbsolutePath (dir.trim()))
            candidates.add (juce::File (dir.trim()).getChildFile ("ffmpeg.exe"));
    candidates.add (juce::File ("D:\\Tools\\ffmpeg\\bin\\ffmpeg.exe"));
    for (const auto& f : candidates)
        if (f.existsAsFile())
            return f;
    return {};
}

AlphaVideo::Info AlphaVideo::parseInfo (const juce::String& output)
{
    Info info;
    juce::StringArray lines;
    lines.addLines (output);
    bool inVideo = false, foundVideo = false;
    for (const auto& raw : lines)
    {
        const auto line = raw.trim();
        if (line.startsWith ("Duration:") && info.seconds <= 0.0)
            info.seconds = parseClock (line.fromFirstOccurrenceOf ("Duration:", false, false).upToFirstOccurrenceOf (",", false, false));
        if (line.startsWith ("Stream #"))
        {
            // Solo el primer video (las caratulas y demas van despues).
            inVideo = ! foundVideo && line.contains (": Video:");
            if (! inVideo)
                continue;
            foundVideo = true;
            info.alpha = pixelFormatHasAlpha (line.fromFirstOccurrenceOf (": Video:", false, false));
            juce::StringArray tokens;
            tokens.addTokens (line.replaceCharacters (",", " "), " ", "");
            for (const auto& t : tokens)
            {
                const auto w = t.upToFirstOccurrenceOf ("x", false, false), h = t.fromFirstOccurrenceOf ("x", false, false);
                // (no los codigos como "0x68347061" de los MOV)
                if (w.isNotEmpty() && h.isNotEmpty() && w.length() <= 5 && h.length() <= 5 && w.containsOnly ("0123456789")
                    && h.containsOnly ("0123456789") && w.getIntValue() > 0 && h.getIntValue() > 0)
                {
                    info.width = w.getIntValue();
                    info.height = h.getIntValue();
                    break;
                }
            }
        }
        // WebM VP9: el alfa va aparte y lo dice su metadato (el formato de pixel sale sin alfa).
        else if (inVideo && line.startsWith ("alpha_mode") && line.fromFirstOccurrenceOf (":", false, false).trim() == "1")
            info.alpha = true;
    }
    return info;
}

bool AlphaVideo::probe (const juce::File& ffmpeg, const juce::File& video, Info& info)
{
    juce::ChildProcess process;
    if (! process.start (juce::StringArray { ffmpeg.getFullPathName(), "-hide_banner", "-i", video.getFullPathName() },
                         juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return false;
    const auto output = process.readAllProcessOutput();
    info = parseInfo (output);
    return output.contains ("Input #0");
}

juce::File AlphaVideo::matteFileFor (const juce::File& video)
{
    const auto name = video.getFileNameWithoutExtension() + "_mate.mp4";
    const auto beside = video.getSiblingFile (name);
    if (beside.existsAsFile() || video.getParentDirectory().hasWriteAccess())
        return beside;
    return dataFolder().getChildFile ("Mate").getChildFile (name);
}

juce::StringArray AlphaVideo::convertArguments (const juce::File& ffmpeg, const juce::File& video, const juce::File& output,
                                                int maxWidth, int maxHeight)
{
    juce::StringArray args { ffmpeg.getFullPathName(), "-hide_banner", "-loglevel", "error", "-nostats", "-y", "-filter_threads", "1" };
    // WebM VP9: el decodificador propio de FFmpeg no lee la transparencia; libvpx si.
    if (video.hasFileExtension ("webm"))
        args.addArray ({ "-c:v", "libvpx-vp9" });
    const auto w = juce::String (maxWidth), h = juce::String (maxHeight);
    const auto filter = "[0:v]scale='min(" + w + ",iw)':'min(" + h + ",ih)':force_original_aspect_ratio=decrease:force_divisible_by=2:flags=lanczos,"
                        "format=yuva444p,split[c][a];[a]alphaextract,format=yuv444p[m];[c]format=yuv444p[cc];"
                        "[cc][m]hstack=inputs=2,format=yuv420p[v]";
    args.addArray ({ "-i", video.getFullPathName(), "-filter_complex", filter, "-map", "[v]", "-map", "0:a?",
                     // Dos hilos: en una CPU lenta, con todos, el audio de MiDJ se corta mientras convierte.
                     "-c:v", "libx264", "-preset", "medium", "-crf", "18", "-threads", "2", "-c:a", "aac", "-b:a", "192k",
                     "-movflags", "+faststart", "-progress", "pipe:1", output.getFullPathName() });
    return args;
}

bool AlphaVideo::convert (const juce::File& ffmpeg, const juce::File& video, const juce::File& output, int maxWidth, int maxHeight,
                          double seconds, const std::function<bool (double)>& progress, juce::String& error)
{
    output.getParentDirectory().createDirectory();
    const auto temp = output.getSiblingFile (output.getFileNameWithoutExtension() + ".convirtiendo.mp4");
    juce::ChildProcess process;
    if (! process.start (convertArguments (ffmpeg, video, temp, maxWidth, maxHeight),
                         juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        error = "No se pudo abrir FFmpeg";
        return false;
    }
    // La salida de -progress: "out_time_us=1234567" cada medio segundo; los errores, tal cual.
    juce::String pending, errors;
    char buffer[4096];
    bool cancelled = false;
    for (;;)
    {
        const int n = process.readProcessOutput (buffer, (int) sizeof (buffer));
        if (n <= 0)
            break;
        pending += juce::String::fromUTF8 (buffer, n);
        while (pending.containsChar ('\n'))
        {
            const auto line = pending.upToFirstOccurrenceOf ("\n", false, false).trim();
            pending = pending.fromFirstOccurrenceOf ("\n", false, false);
            if (line.startsWith ("out_time_us=") || line.startsWith ("out_time_ms="))
            {
                const double done = line.fromFirstOccurrenceOf ("=", false, false).getLargeIntValue() / 1.0e6;
                if (progress && ! progress (seconds > 0.0 ? juce::jlimit (0.0, 1.0, done / seconds) : 0.0))
                    cancelled = true;
            }
            else if (line.isNotEmpty() && ! line.containsChar ('='))
                errors << line << "\n";
            if (cancelled)
                break;
        }
        if (cancelled)
            break;
    }
    if (cancelled)
    {
        process.kill();
        process.waitForProcessToFinish (5000);
        temp.deleteFile();
        error = "Cancelado";
        return false;
    }
    process.waitForProcessToFinish (10000);
    if (process.getExitCode() != 0 || ! temp.existsAsFile() || temp.getSize() == 0)
    {
        temp.deleteFile();
        error = "FFmpeg no pudo convertirlo" + (errors.isNotEmpty() ? ": " + errors.trim().fromLastOccurrenceOf ("\n", false, false) : juce::String());
        return false;
    }
    output.deleteFile();
    if (! temp.moveFileTo (output))
    {
        temp.deleteFile();
        error = "No se pudo guardar " + output.getFullPathName();
        return false;
    }
    return true;
}
}
