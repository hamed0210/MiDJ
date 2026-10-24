#include "UI/AlphaConvertDialog.h"
#include "Video/AlphaVideo.h"

namespace midj
{
namespace
{
    int programWidth = 1280, programHeight = 720;
    using Done = std::function<void (const juce::File&, bool)>;

    void convertNow (const juce::File& ffmpeg, const juce::File& chosen, Done done);

    // Descarga FFmpeg (zip) y saca ffmpeg.exe a la carpeta de datos.
    class DownloadFfmpeg : public juce::ThreadWithProgressWindow
    {
    public:
        DownloadFfmpeg (juce::File video, Done d)
            : ThreadWithProgressWindow ("Descargando FFmpeg...", true, true, 10000, "Cancelar"),
              chosen (std::move (video)), done (std::move (d)) {}

        void run() override
        {
            const auto target = AlphaVideo::downloadedFfmpeg();
            target.getParentDirectory().createDirectory();
            const auto zipFile = target.getSiblingFile ("ffmpeg-descarga.zip");
            {
                int status = 0;
                auto stream = juce::URL (AlphaVideo::ffmpegDownloadUrl)
                                  .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                          .withConnectionTimeoutMs (20000)
                                                          .withNumRedirectsToFollow (5)
                                                          .withStatusCode (&status));
                if (stream == nullptr || (status != 0 && status != 200))
                {
                    error = "No se pudo descargar FFmpeg (sin conexion con GitHub" + (status > 0 ? ", respuesta " + juce::String (status) : juce::String()) + ")";
                    return;
                }
                zipFile.deleteFile();
                juce::FileOutputStream out (zipFile);
                if (! out.openedOk())
                {
                    error = "No se pudo escribir en " + zipFile.getParentDirectory().getFullPathName();
                    return;
                }
                const auto total = stream->getTotalLength();
                juce::HeapBlock<char> buffer (1 << 16);
                juce::int64 got = 0;
                for (;;)
                {
                    if (threadShouldExit())
                    {
                        out.flush();
                        break;
                    }
                    const int n = stream->read (buffer, 1 << 16);
                    if (n <= 0)
                        break;
                    out.write (buffer, (size_t) n);
                    got += n;
                    setProgress (total > 0 ? (double) got / (double) total : -1.0);
                    setStatusMessage ("FFmpeg (para convertir videos con transparencia): "
                                      + juce::File::descriptionOfSizeInBytes (got)
                                      + (total > 0 ? " de " + juce::File::descriptionOfSizeInBytes (total) : juce::String()));
                }
            }
            if (threadShouldExit())
            {
                zipFile.deleteFile();
                return;
            }
            setStatusMessage ("Descomprimiendo FFmpeg...");
            setProgress (-1.0);
            {
                juce::ZipFile zip (zipFile);
                for (int i = 0; i < zip.getNumEntries(); ++i)
                {
                    const auto* entry = zip.getEntry (i);
                    if (entry == nullptr || ! entry->filename.replaceCharacter ('\\', '/').endsWithIgnoreCase ("/bin/ffmpeg.exe"))
                        continue;
                    std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
                    const auto part = target.getSiblingFile ("ffmpeg.exe.part");
                    part.deleteFile();
                    bool written = false;
                    if (in != nullptr)
                    {
                        juce::FileOutputStream out (part);
                        written = out.openedOk() && out.writeFromInputStream (*in, -1) > 0;
                    }
                    if (written && (target.deleteFile(), part.moveFileTo (target)))
                        break;
                    part.deleteFile();
                }
            }
            zipFile.deleteFile();
            if (! target.existsAsFile())
                error = "La descarga de FFmpeg no trae ffmpeg.exe";
        }

        void threadComplete (bool cancelled) override
        {
            const auto ffmpeg = AlphaVideo::downloadedFfmpeg();
            if (! cancelled && error.isEmpty() && ffmpeg.existsAsFile())
                convertNow (ffmpeg, chosen, done);
            else
            {
                if (! cancelled)
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Video con transparencia",
                                                            error + ".\nSe usa el video tal cual (sin transparencia).");
                done (chosen, false);
            }
            delete this;
        }

    private:
        juce::File chosen;
        Done done;
        juce::String error;
    };

    // Mira si el video tiene transparencia y, si la tiene, lo convierte.
    class Convert : public juce::ThreadWithProgressWindow
    {
    public:
        Convert (juce::File ff, juce::File video, Done d)
            : ThreadWithProgressWindow ("Video con transparencia", true, true, 10000, "Cancelar"),
              ffmpeg (std::move (ff)), chosen (std::move (video)), output (AlphaVideo::matteFileFor (chosen)), done (std::move (d)) {}

        void run() override
        {
            setStatusMessage ("Mirando " + chosen.getFileName() + "...");
            setProgress (-1.0);
            AlphaVideo::Info info;
            if (! AlphaVideo::probe (ffmpeg, chosen, info))
            {
                error = "FFmpeg no pudo leer el video";
                return;
            }
            if (! info.alpha)
                return;   // sin transparencia: el video tal cual
            hasAlpha = true;
            setStatusMessage ("Convirtiendo " + chosen.getFileName() + " a un video con mate para MiDJ...");
            setProgress (0.0);
            converted = AlphaVideo::convert (ffmpeg, chosen, output, programWidth, programHeight, info.seconds,
                                             [this] (double p) { setProgress (p); return ! threadShouldExit(); }, error);
        }

        void threadComplete (bool cancelled) override
        {
            if (converted)
                done (output, true);
            else
            {
                if (! cancelled && error.isNotEmpty() && error != "Cancelado")
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Video con transparencia",
                                                            error + ".\nSe usa el video tal cual (sin transparencia).");
                done (chosen, false);
            }
            delete this;
        }

    private:
        juce::File ffmpeg, chosen, output;
        Done done;
        juce::String error;
        bool hasAlpha = false, converted = false;
    };

    void convertNow (const juce::File& ffmpeg, const juce::File& chosen, Done done)
    {
        (new Convert (ffmpeg, chosen, std::move (done)))->launchThread();
    }
}

void AlphaConvertDialog::setProgramSize (int width, int height)
{
    programWidth = juce::jmax (16, width);
    programHeight = juce::jmax (16, height);
}

void AlphaConvertDialog::prepare (const juce::File& chosen, Done done)
{
    if (! AlphaVideo::mightHaveAlpha (chosen) || ! chosen.existsAsFile())
    {
        done (chosen, false);
        return;
    }
    // Ya convertido antes: ese.
    if (const auto converted = AlphaVideo::matteFileFor (chosen); converted.existsAsFile())
    {
        done (converted, true);
        return;
    }
    if (const auto ffmpeg = AlphaVideo::findFfmpeg(); ffmpeg.existsAsFile())
    {
        convertNow (ffmpeg, chosen, std::move (done));
        return;
    }
    // Sin FFmpeg: preguntar si se descarga.
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
        juce::MessageBoxIconType::QuestionIcon, "Video con transparencia",
        chosen.getFileName() + " puede tener transparencia. Para que MiDJ la use hay que convertirlo con FFmpeg "
        "(gratis), que no esta en este equipo.\n\nDescargarlo ahora? Son unos 200 MB, una sola vez (de GitHub); "
        "solo se guarda ffmpeg.exe.",
        "Descargar y convertir", "Usar el video tal cual");
    juce::AlertWindow::showAsync (options, [chosen, done] (int result)
    {
        if (result == 1)
            (new DownloadFfmpeg (chosen, done))->launchThread();
        else
            done (chosen, false);
    });
}
}
