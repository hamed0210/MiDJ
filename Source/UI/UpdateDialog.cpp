#include "UI/UpdateDialog.h"
#include "App/Updates.h"
#include "Library/Library.h"

namespace midj
{
namespace
{
    juce::File settingsFile() { return Library::getDefaultFile().getSiblingFile ("updates.xml"); }

    // Baja el instalador a la carpeta temporal; al terminar lo abre y cierra MiDJ.
    class DownloadInstaller : public juce::ThreadWithProgressWindow
    {
    public:
        explicit DownloadInstaller (Updates::Release r)
            : ThreadWithProgressWindow ("Descargando MiDJ " + r.version + "...", true, true, 10000, "Cancelar"),
              release (std::move (r)),
              target (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("MiDJ-Setup-" + release.version + ".exe")) {}

        void run() override
        {
            int status = 0;
            auto stream = juce::URL (release.installerUrl)
                              .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                      .withConnectionTimeoutMs (20000)
                                                      .withNumRedirectsToFollow (5)
                                                      .withStatusCode (&status)
                                                      .withExtraHeaders ("User-Agent: MiDJ\r\n"));
            if (stream == nullptr || (status != 0 && status != 200))
            {
                error = "No se pudo descargar el instalador (sin conexion con GitHub" + (status > 0 ? ", respuesta " + juce::String (status) : juce::String()) + ")";
                return;
            }
            const auto part = target.getSiblingFile (target.getFileName() + ".part");
            part.deleteFile();
            juce::int64 got = 0;
            {
                juce::FileOutputStream out (part);
                if (! out.openedOk())
                {
                    error = "No se pudo escribir en " + part.getParentDirectory().getFullPathName();
                    return;
                }
                const auto total = release.installerSize > 0 ? release.installerSize : stream->getTotalLength();
                juce::HeapBlock<char> buffer (1 << 16);
                while (! threadShouldExit())
                {
                    const int n = stream->read (buffer, 1 << 16);
                    if (n <= 0)
                        break;
                    out.write (buffer, (size_t) n);
                    got += n;
                    setProgress (total > 0 ? (double) got / (double) total : -1.0);
                    setStatusMessage ("Instalador de MiDJ " + release.version + ": " + juce::File::descriptionOfSizeInBytes (got)
                                      + (total > 0 ? " de " + juce::File::descriptionOfSizeInBytes (total) : juce::String()));
                }
            }
            if (threadShouldExit())
            {
                part.deleteFile();
                return;
            }
            // Incompleto (se corto la conexion): no se abre un instalador a medias.
            if (got == 0 || (release.installerSize > 0 && got != release.installerSize))
            {
                part.deleteFile();
                error = "La descarga se corto antes de terminar";
                return;
            }
            target.deleteFile();
            if (! part.moveFileTo (target))
                error = "No se pudo guardar " + target.getFullPathName();
        }

        void threadComplete (bool cancelled) override
        {
            if (! cancelled && error.isEmpty() && target.existsAsFile())
            {
                if (target.startAsProcess())
                    juce::JUCEApplication::getInstance()->systemRequestedQuit();   // el instalador cierra MiDJ igual; asi se guarda todo antes
                else
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Actualizar MiDJ",
                                                            "No se pudo abrir el instalador. Esta en:\n" + target.getFullPathName());
            }
            else if (! cancelled)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Actualizar MiDJ",
                                                        error + ".\nTambien se puede bajar desde " + release.pageUrl);
            delete this;
        }

    private:
        Updates::Release release;
        juce::File target;
        juce::String error;
    };

    void offer (const Updates::Release& release, juce::Component* parent)
    {
        auto notes = Updates::plainNotes (release.notes);
        // Las de los instaladores empiezan con "Instalar o actualizar": eso ya lo dice este aviso.
        if (const int i = notes.indexOf ("Novedades"); i > 0)
            notes = notes.substring (i + 9).trimStart();
        if (notes.length() > 1200)
            notes = notes.substring (0, 1200).upToLastOccurrenceOf ("\n", false, false) + "\n...";

        auto* window = new juce::AlertWindow ("MiDJ " + release.version + " disponible",
                                              "Tienes la " + UpdateDialog::currentVersion() + ". Tus canciones, escenas y ajustes se conservan al actualizar.",
                                              juce::MessageBoxIconType::InfoIcon, parent);
        if (notes.isNotEmpty())
            window->addTextBlock (notes);
        // Sin el instalador adjunto (version mal publicada) solo queda bajarla de su pagina.
        if (release.installerUrl.isNotEmpty())
            window->addButton ("Descargar e instalar", 1, juce::KeyPress (juce::KeyPress::returnKey));
        else
            window->addButton ("Descargar", 2, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Omitir esta version", 3);
        window->addButton ("Mas tarde", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([release] (int result)
        {
            if (result == 1)
                (new DownloadInstaller (release))->launchThread();
            else if (result == 2)
                juce::URL (release.pageUrl.isNotEmpty() ? release.pageUrl : Updates::releasesPage()).launchInDefaultBrowser();
            else if (result == 3)
            {
                auto s = Updates::Settings::load (settingsFile());
                s.skippedVersion = release.version;
                s.save (settingsFile());
            }
        }), true);
    }

    void check (juce::Component& parent, bool manual)
    {
        juce::Component::SafePointer<juce::Component> safe (&parent);
        juce::Thread::launch ([safe, manual]
        {
            Updates::Release release;
            juce::String error;
            const bool ok = Updates::fetchLatest (release, error);
            juce::MessageManager::callAsync ([safe, manual, ok, release, error]
            {
                if (safe == nullptr)
                    return;
                auto s = Updates::Settings::load (settingsFile());
                if (ok)
                {
                    s.lastCheckMs = juce::Time::currentTimeMillis();
                    s.save (settingsFile());
                }
                const bool newer = ok && Updates::compareVersions (release.version, UpdateDialog::currentVersion()) > 0;
                if (newer && (manual || release.version != s.skippedVersion))
                    offer (release, safe.getComponent());
                else if (manual)
                    juce::AlertWindow::showMessageBoxAsync (ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                                            "Actualizaciones",
                                                            ok ? "Tienes la ultima version de MiDJ (" + UpdateDialog::currentVersion() + ")."
                                                               : "No se pudo buscar: " + error + ".\nLas versiones estan en " + Updates::releasesPage(),
                                                            {}, safe.getComponent());
            });
        });
    }
}

void UpdateDialog::checkOnStartup (juce::Component& parent)
{
    if (Updates::Settings::load (settingsFile()).dueForCheck (juce::Time::currentTimeMillis()))
        check (parent, false);
}

void UpdateDialog::checkNow (juce::Component& parent) { check (parent, true); }

bool UpdateDialog::getCheckOnStartup() { return Updates::Settings::load (settingsFile()).checkOnStartup; }

void UpdateDialog::setCheckOnStartup (bool on)
{
    auto s = Updates::Settings::load (settingsFile());
    s.checkOnStartup = on;
    s.save (settingsFile());
}

juce::String UpdateDialog::currentVersion()
{
    if (auto* app = juce::JUCEApplicationBase::getInstance())
        return app->getApplicationVersion();
    return {};
}
}
