#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <wrl.h>
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"

#include "UI/YouTubePlayer.h"
#include "Video/Scenes.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace
{
    // Se inyecta en la pagina de YouTube antes de que cargue. Deja solo el video a toda la
    // ventana, pide H.264 (la grafica lo decodifica; VP9 y AV1 irian por la CPU), informa a MiDJ
    // cuatro veces por segundo y obedece sus ordenes.
    const char* const builtInScript = R"midjjs(
(() => {
  if (window.__midj) return;
  window.__midj = true;

  const bad = /vp0?9|av01/i;
  if (window.MediaSource && MediaSource.isTypeSupported) {
    const orig = MediaSource.isTypeSupported.bind(MediaSource);
    MediaSource.isTypeSupported = (t) => (bad.test(t) ? false : orig(t));
  }
  const origCan = HTMLMediaElement.prototype.canPlayType;
  HTMLMediaElement.prototype.canPlayType = function (t) { return bad.test(t) ? '' : origCan.call(this, t); };

  const css = `
    html, body { overflow: hidden !important; background: #000 !important; }
    #masthead-container, #secondary, #below, #comments, ytd-mini-guide-renderer, tp-yt-app-drawer,
    .ytp-chrome-bottom, .ytp-chrome-top, .ytp-gradient-bottom, .ytp-gradient-top, .ytp-ce-element,
    .ytp-pause-overlay, .ytp-paid-content-overlay, .ytp-watermark, .iv-branding, .ytp-endscreen-content,
    .ytp-cards-teaser, .ytp-spinner, .ytp-tooltip, .ytp-bezel, .ytp-contextmenu, ytd-popup-container,
    #chat, .ytp-suggested-action, .ytp-autonav-endscreen-countdown-overlay, .ytp-upnext,
    tp-yt-iron-overlay-backdrop, .ytp-cued-thumbnail-overlay,
    .ytp-caption-window-container, .caption-window, .ytp-caption-segment { display: none !important; }
    #movie_player, #movie_player.ytp-fullscreen {
      position: fixed !important; inset: 0 !important; width: 100vw !important; height: 100vh !important;
      z-index: 2147483647 !important; background: #000 !important; }
    #movie_player .html5-video-container video {
      position: fixed !important; inset: 0 !important; width: 100vw !important; height: 100vh !important;
      object-fit: contain !important; left: 0 !important; top: 0 !important; }`;
  const onYouTube = /(^|\.)youtube\.com$/.test(location.hostname);
  let verifying = false;   // la ventana esta a la vista para que el usuario resuelva una verificacion
  const addCss = () => {
    const old = document.getElementById('midj-css');
    if (verifying || !onYouTube) { if (old) old.remove(); return; }   // la pagina tal cual
    if (old || !document.head) return;
    const s = document.createElement('style'); s.id = 'midj-css'; s.textContent = css; document.head.appendChild(s);
  };
  // Google pide demostrar que no es un robot (pagina /sorry con reCAPTCHA) o YouTube pide iniciar
  // sesion "para confirmar que no eres un bot": eso lo tiene que resolver una persona.
  const captchaPage = () => (/(^|\.)google\.[a-z.]+$/.test(location.hostname) && location.pathname.startsWith('/sorry'))
    || !!document.querySelector('#captcha-form, iframe[src*="recaptcha"]');
  const botCheck = /\bbot\b|robot/i;
  const skipSelector = '.ytp-skip-ad-button, .ytp-ad-skip-button, .ytp-ad-skip-button-modern, .ytp-ad-skip-button-container button, '
    + '.videoAdUiSkipButton, button[id^="skip-button"], .ytp-ad-overlay-close-button';
  // Saltar el anuncio que se ve: el boton Omitir si esta; si no, sin sonido y al final.
  const skipNow = (v) => {
    document.querySelectorAll(skipSelector).forEach((b) => { try { b.click(); } catch (e) {} });
    try { v.muted = true; v.playbackRate = 16; if (isFinite(v.duration) && v.duration > 0 && v.currentTime < v.duration - 0.3) v.currentTime = v.duration - 0.1; } catch (e) {}
  };
  document.addEventListener('DOMContentLoaded', addCss);

  const post = (o) => { try { window.chrome.webview.postMessage(JSON.stringify(o)); } catch (e) {} };
  // El video de esta pagina: MiDJ descarta los avisos que no son del que pidio (los de la pagina
  // anterior llegan un rato despues de cargar el siguiente).
  const query = new URLSearchParams(location.search);
  let pageId = (query.get('v') || '') + '|' + (query.get('midj') || '');   // el video y que vez se abrio
  if (query.get('midj')) sessionStorage.setItem('midjPage', pageId);
  else { const saved = sessionStorage.getItem('midjPage') || ''; if (saved.startsWith((query.get('v') || '') + '|')) pageId = saved; }   // recargada sin &midj
  let wantedVolume = -1, appliedVolume = -1, lastKick = 0, userPaused = false, finished = false;
  let wantedQuality = 'hd720', appliedQuality = '', skipAds = false, inAd = false;
  let captionsOff = false;
  let stuckSince = 0, reloaded = sessionStorage.getItem('midjReloaded') === location.href;
  setInterval(() => {
    addCss();
    const p = document.getElementById('movie_player');
    const v = document.querySelector('#movie_player video');
    const ad = !!(p && p.classList.contains('ad-showing'));
    let state = 'opening';
    if (p && v) {
      const now = performance.now();
      // Saltar anuncios (si MiDJ lo pide).
      if (ad && skipAds) {
        inAd = true;
        skipNow(v);
      } else if (inAd && !ad) {
        inAd = false;   // termino el anuncio: velocidad y sonido normales
        try { v.playbackRate = 1; v.muted = false; } catch (e) {}
        appliedVolume = -1;
      }
      // Calidad: solo con el video ya sonando (pedirla durante un anuncio deja el video sin cargar).
      if (wantedQuality !== appliedQuality && !ad && v.currentTime > 1 && p.setPlaybackQualityRange) {
        appliedQuality = wantedQuality;
        try { p.setPlaybackQualityRange(wantedQuality, wantedQuality); if (p.setPlaybackQuality) p.setPlaybackQuality(wantedQuality); } catch (e) {}
      }
      // Subtitulos apagados (tambien los automaticos): la salida de video no los muestra.
      if (!captionsOff && !ad && v.currentTime > 0.5) {
        captionsOff = true;
        try { if (p.unloadModule) { p.unloadModule('captions'); p.unloadModule('cc'); } } catch (e) {}
      }
      if (wantedVolume >= 0 && wantedVolume !== appliedVolume && !(ad && skipAds)) { appliedVolume = wantedVolume; try { p.setVolume(Math.round(wantedVolume * 100)); p.unMute(); } catch (e) {} }
      // Otro video en la misma pagina (la reproduccion automatica de YouTube): este ya termino.
      const otherVideo = location.pathname === '/watch' && new URLSearchParams(location.search).get('v') !== query.get('v');
      const ended = !ad && (v.ended || otherVideo || (p.getPlayerState && p.getPlayerState() === 0));
      if (ended) finished = true;
      // Terminado se queda terminado: ni play automatico ni el siguiente de YouTube (ni repetirlo).
      if (finished) {
        if (!v.paused) { try { p.pauseVideo(); v.pause(); } catch (e) {} }
        state = 'ended';
      } else {
        // Parado al principio (sin autoplay, o tras los anuncios): pedirle play cada 4 s (salvo pausa de MiDJ).
        if (!ad && !userPaused && v.paused && v.currentTime < 0.5 && now - lastKick > 4000 && now > 4000) { lastKick = now; try { p.playVideo(); } catch (e) {} }
        // Atascado sin datos (tras un anuncio): a los 8 s, play otra vez; a los 16 s, recargar (una vez).
        const stuck = !ad && !userPaused && v.readyState < 2 && v.currentTime < 0.5;
        if (!stuck) stuckSince = 0;
        else if (stuckSince === 0) stuckSince = now;
        else if (now - stuckSince > 16000 && !reloaded) { reloaded = true; sessionStorage.setItem('midjReloaded', location.href); location.reload(); }
        else if (now - stuckSince > 8000 && now - lastKick > 4000) { lastKick = now; try { p.playVideo(); } catch (e) {} }
        state = ad ? 'ad' : v.paused ? (v.currentTime > 0 ? 'paused' : 'opening') : 'playing';
      }
    }
    const errEl = document.querySelector('.ytp-error-content-wrap-reason, yt-playability-error-supported-renderers #reason, .ytp-error');
    const err = errEl ? errEl.innerText.trim().slice(0, 200) : '';
    if (err && !finished) state = botCheck.test(err) ? 'verify' : 'error';
    // La pagina de Google no es la del video (otro id): se avisa aparte.
    if (captchaPage()) { post({ captcha: true }); return; }
    post({ id: pageId, state, err, t: v ? v.currentTime : 0, d: v && !ad && isFinite(v.duration) ? v.duration : 0 });
  }, 250);

  window.chrome.webview.addEventListener('message', (e) => {
    const p = document.getElementById('movie_player');
    const m = e.data;
    if (m.cmd === 'volume') { wantedVolume = m.value; return; }
    if (m.cmd === 'settings') { wantedQuality = m.quality || 'auto'; skipAds = !!m.skipAds; return; }
    if (m.cmd === 'verifying') { verifying = !!m.value; addCss(); return; }
    if (!p) return;
    if (m.cmd === 'skipAd') {
      const v = document.querySelector('#movie_player video');
      if (v && p.classList.contains('ad-showing')) { inAd = true; skipNow(v); }
      return;
    }
    try {
      if (m.cmd === 'play') { userPaused = false; if (!finished) p.playVideo(); }
      else if (m.cmd === 'pause') { userPaused = true; p.pauseVideo(); }
      else if (m.cmd === 'seek') { if (!finished) p.seekTo(m.value, true); }
    } catch (err) {}
  });
})();
)midjjs";

    // El cuadro de la ventana del reproductor: a la derecha de todas las pantallas.
    POINT offscreenPosition()
    {
        return { GetSystemMetrics (SM_XVIRTUALSCREEN) + GetSystemMetrics (SM_CXVIRTUALSCREEN) + 200,
                 GetSystemMetrics (SM_YVIRTUALSCREEN) };
    }

    // Edge decide que una ventana fuera de la pantalla esta tapada y no reproduce: se le quita.
    const wchar_t* const browserArguments = L"--autoplay-policy=no-user-gesture-required "
                                            L"--disable-features=CalculateNativeWinOcclusion "
                                            L"--disable-backgrounding-occluded-windows --disable-renderer-backgrounding "
                                            L"--disable-background-timer-throttling";

    std::wstring wide (const juce::String& s) { return s.toWideCharPointer(); }
}

struct YouTubePlayer::Impl
{
    HWND window = nullptr;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webView;
    bool creatingEnvironment = false, creatingController = false;
    std::vector<std::function<void (bool)>> environmentWaiters;
    // La ventana de la cuenta (inicio de sesion de Google), con su propio WebView en el mismo perfil.
    HWND accountWindow = nullptr;
    ComPtr<ICoreWebView2Controller> accountController;
    ComPtr<ICoreWebView2> accountView;
    bool signingIn = false;   // se abrio para iniciar sesion: se cierra sola al volver a YouTube con ella
    juce::String pendingId;   // el video a cargar en cuanto este listo
    int loadCount = 0;        // va en la direccion (&midj=): los avisos de una pagina anterior se descartan
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);   // para los avisos de WebView2 que llegan tarde

    static void registerClass()
    {
        static bool done = false;
        if (done)
            return;
        WNDCLASSW wc {};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW (nullptr);
        wc.lpszClassName = L"MiDJYouTubePlayer";
        wc.hbrBackground = (HBRUSH) GetStockObject (BLACK_BRUSH);
        RegisterClassW (&wc);
        WNDCLASSW account {};
        account.lpfnWndProc = accountProc;
        account.hInstance = GetModuleHandleW (nullptr);
        account.lpszClassName = L"MiDJYouTubeAccount";
        account.hbrBackground = (HBRUSH) GetStockObject (WHITE_BRUSH);
        account.hCursor = LoadCursor (nullptr, IDC_ARROW);
        account.hIcon = LoadIconW (GetModuleHandleW (nullptr), L"IDI_ICON1");   // el de MiDJ.exe (recurso con nombre de JUCE)
        RegisterClassW (&account);
        done = true;
    }

    // La ventana de la cuenta: el WebView sigue su tamano y cerrarla la destruye (con su WebView).
    static LRESULT CALLBACK accountProc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        auto* player = reinterpret_cast<YouTubePlayer*> (GetWindowLongPtrW (hwnd, GWLP_USERDATA));
        if (player != nullptr && msg == WM_SIZE && player->impl->accountController != nullptr)
        {
            RECT rc {};
            GetClientRect (hwnd, &rc);
            player->impl->accountController->put_Bounds (rc);
        }
        if (player != nullptr && msg == WM_CLOSE)
        {
            player->closeAccount();
            return 0;
        }
        return DefWindowProcW (hwnd, msg, wp, lp);
    }

    void createWindow (int width, int height)
    {
        if (window != nullptr)
            return;
        registerClass();
        // Ventana normal sin duenno y sin bordes, fuera de las pantallas. Windows Graphics Capture
        // rechaza las ventanas de herramientas, con duenno o quitadas de la barra de tareas (las que
        // no saldrian con Alt+Tab): por eso aparece agrupada bajo el icono de MiDJ.
        const auto pos = offscreenPosition();
        // Se muestra sin activarla: si no, se queda con el teclado y los atajos de MiDJ no llegan.
        window = CreateWindowExW (0, L"MiDJYouTubePlayer", wide (YouTubeWindow::title).c_str(), WS_POPUP,
                                  pos.x, pos.y, width, height, nullptr, nullptr, GetModuleHandleW (nullptr), nullptr);
        if (window != nullptr)
            ShowWindow (window, SW_SHOWNOACTIVATE);
    }

    bool verifying = false;   // la ventana esta a la vista para resolver una verificacion

    // Poner la ventana en el centro de la pantalla principal (con barra de titulo, sin cerrar:
    // cerrarla la destruiria) o devolverla fuera de las pantallas a su tamano de captura.
    void placeWindow (bool visible, int width, int height)
    {
        if (window == nullptr)
            return;
        RECT work {};
        SystemParametersInfoW (SPI_GETWORKAREA, 0, &work, 0);
        const DWORD style = visible ? (WS_POPUP | WS_CAPTION | WS_VISIBLE) : (WS_POPUP | WS_VISIBLE);
        SetWindowLongPtrW (window, GWL_STYLE, (LONG_PTR) style);
        RECT client { 0, 0, visible ? juce::jmin (1100, (int) (work.right - work.left) - 80) : width,
                            visible ? juce::jmin (800, (int) (work.bottom - work.top) - 80) : height };
        RECT frame = client;
        AdjustWindowRectEx (&frame, style, FALSE, 0);
        const int fw = frame.right - frame.left, fh = frame.bottom - frame.top;
        const auto off = offscreenPosition();
        const int x = visible ? (int) (work.left + work.right - fw) / 2 : off.x;
        const int y = visible ? (int) (work.top + work.bottom - fh) / 2 : off.y;
        SetWindowPos (window, visible ? HWND_TOPMOST : HWND_NOTOPMOST, x, y, fw, fh,
                      SWP_FRAMECHANGED | SWP_SHOWWINDOW | (visible ? 0 : SWP_NOACTIVATE));
        if (visible)
        {
            SetForegroundWindow (window);
            // Encima solo al aparecer: luego se puede pasar a MiDJ.
            SetWindowPos (window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        if (controller != nullptr)
        {
            RECT rc { 0, 0, client.right, client.bottom };
            controller->put_Bounds (rc);
            if (visible)
                controller->MoveFocus (COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        }
        verifying = visible;
    }

    void destroyWindow()
    {
        verifying = false;
        if (controller != nullptr)
            controller->Close();
        controller.Reset();
        webView.Reset();
        creatingController = false;
        if (window != nullptr)
            DestroyWindow (window);
        window = nullptr;
    }
};

YouTubePlayer::YouTubePlayer (const juce::File& folder, int w, int h)
    : impl (std::make_unique<Impl>()), dataFolder (folder), width (w), height (h) {}

YouTubePlayer::~YouTubePlayer()
{
    *impl->alive = false;
    impl->destroyWindow();
    if (impl->accountController != nullptr)
        impl->accountController->Close();
    if (impl->accountWindow != nullptr)
        DestroyWindow (impl->accountWindow);
}

YouTubePlayer::State YouTubePlayer::stateFromText (const juce::String& s)
{
    if (s == "ad")      return State::ad;
    if (s == "playing") return State::playing;
    if (s == "paused")  return State::paused;
    if (s == "ended")   return State::ended;
    if (s == "error")   return State::error;
    if (s == "verify")  return State::verify;
    return State::opening;
}

juce::String YouTubePlayer::stateText (const Status& s)
{
    switch (s.state)
    {
        case State::verify:  return "YouTube pide verificar que no eres un robot";
        case State::closed:  return "Sin video";
        case State::opening: return "Cargando...";
        case State::ad:      return "Anuncio";
        case State::playing: return "Sonando";
        case State::paused:  return "En pausa";
        case State::ended:   return "Termino";
        case State::error:   return s.error.isNotEmpty() ? s.error : juce::String ("No se puede reproducir");
    }
    return {};
}

void YouTubePlayer::setState (State s)
{
    if (status.state != s)
    {
        status.state = s;
        sendChangeMessage();
    }
}

void YouTubePlayer::open (const YouTubeVideo& v)
{
    video = v;
    status = {};
    status.state = State::opening;
    status.duration = v.seconds;
    sendChangeMessage();
    impl->pendingId = v.id;
    ++impl->loadCount;
    impl->createWindow (width, height);

    auto navigate = [this]
    {
        if (impl->webView == nullptr || impl->pendingId.isEmpty())
            return;
        impl->webView->Navigate (wide ("https://www.youtube.com/watch?v=" + impl->pendingId + "&midj=" + juce::String (impl->loadCount)).c_str());
        impl->pendingId.clear();
    };
    if (impl->webView != nullptr)
    {
        navigate();
        return;
    }

    std::weak_ptr<bool> alive = impl->alive;
    auto createController = [this, alive, navigate]
    {
        if (impl->creatingController || impl->environment == nullptr)
            return;
        impl->creatingController = true;
        impl->environment->CreateCoreWebView2Controller (impl->window, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler> (
            [this, alive, navigate] (HRESULT hr, ICoreWebView2Controller* c) -> HRESULT
            {
                if (alive.expired() || ! *alive.lock())
                    return S_OK;
                impl->creatingController = false;
                if (FAILED (hr) || c == nullptr || impl->window == nullptr)
                {
                    status.error = "No se pudo abrir el reproductor (WebView2)";
                    setState (State::error);
                    return S_OK;
                }
                impl->controller = c;
                impl->controller->get_CoreWebView2 (&impl->webView);
                RECT rc { 0, 0, width, height };
                impl->controller->put_Bounds (rc);
                // El script: el de la carpeta de datos si lo hay (para arreglarlo sin recompilar).
                const auto custom = dataFolder.getChildFile ("youtube.js");
                const auto script = custom.existsAsFile() ? custom.loadFileAsString() : juce::String (builtInScript);
                impl->webView->AddScriptToExecuteOnDocumentCreated (wide (script).c_str(), nullptr);
                impl->webView->add_WebMessageReceived (Callback<ICoreWebView2WebMessageReceivedEventHandler> (
                    [this, alive] (ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT
                    {
                        LPWSTR text = nullptr;
                        if (! alive.expired() && *alive.lock() && SUCCEEDED (args->TryGetWebMessageAsString (&text)) && text != nullptr)
                            handleMessage (juce::String (text));
                        CoTaskMemFree (text);
                        return S_OK;
                    }).Get(), nullptr);
                // La sesion de YouTube (por si caduca o se inicio en otro sitio).
                impl->webView->add_NavigationCompleted (Callback<ICoreWebView2NavigationCompletedEventHandler> (
                    [this, alive] (ICoreWebView2* view, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT
                    {
                        if (! alive.expired() && *alive.lock())
                            checkSignedIn (view);
                        return S_OK;
                    }).Get(), nullptr);
                navigate();
                return S_OK;
            }).Get());
    };

    withEnvironment ([this, createController] (bool ok)
    {
        if (! ok)
        {
            status.error = "Falta el WebView2 Runtime de Microsoft";
            setState (State::error);
        }
        else if (impl->window != nullptr)
        {
            createController();
        }
    });
}

void YouTubePlayer::withEnvironment (std::function<void (bool)> then)
{
    if (impl->environment != nullptr)
    {
        then (true);
        return;
    }
    impl->environmentWaiters.push_back (std::move (then));
    if (impl->creatingEnvironment)
        return;
    impl->creatingEnvironment = true;
    auto finish = [this] (bool ok)
    {
        impl->creatingEnvironment = false;
        auto waiters = std::move (impl->environmentWaiters);
        impl->environmentWaiters.clear();
        for (auto& w : waiters)
            w (ok);
    };
    std::weak_ptr<bool> alive = impl->alive;
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    options->put_AdditionalBrowserArguments (browserArguments);
    const auto profile = dataFolder.getChildFile ("WebView2");
    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions (nullptr, wide (profile.getFullPathName()).c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler> (
            [this, alive, finish] (HRESULT result, ICoreWebView2Environment* env) -> HRESULT
            {
                if (alive.expired() || ! *alive.lock())
                    return S_OK;
                if (SUCCEEDED (result) && env != nullptr)
                    impl->environment = env;
                finish (impl->environment != nullptr);
                return S_OK;
            }).Get());
    if (FAILED (hr))
        finish (false);
}

// --- Cuenta ----------------------------------------------------------------------------------------

void YouTubePlayer::openAccountWindow (bool visible, std::function<void()> ready)
{
    if (impl->accountWindow != nullptr)
    {
        if (visible)
        {
            ShowWindow (impl->accountWindow, IsIconic (impl->accountWindow) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow (impl->accountWindow);
        }
        if (impl->accountView != nullptr)
            ready();
        return;
    }
    Impl::registerClass();
    RECT work {};
    SystemParametersInfoW (SPI_GETWORKAREA, 0, &work, 0);
    const int w = juce::jmin (560, (int) (work.right - work.left) - 40), h = juce::jmin (760, (int) (work.bottom - work.top) - 40);
    impl->accountWindow = CreateWindowExW (0, L"MiDJYouTubeAccount", L"MiDJ - Cuenta de YouTube", WS_OVERLAPPEDWINDOW,
                                           (int) (work.left + work.right - w) / 2, (int) (work.top + work.bottom - h) / 2, w, h,
                                           nullptr, nullptr, GetModuleHandleW (nullptr), nullptr);
    if (impl->accountWindow == nullptr)
        return;
    SetWindowLongPtrW (impl->accountWindow, GWLP_USERDATA, (LONG_PTR) this);
    if (visible)
    {
        ShowWindow (impl->accountWindow, SW_SHOW);
        SetForegroundWindow (impl->accountWindow);
    }

    std::weak_ptr<bool> alive = impl->alive;
    withEnvironment ([this, alive, ready] (bool ok)
    {
        if (impl->accountWindow == nullptr)
            return;
        if (! ok)
        {
            closeAccount();
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Cuenta de YouTube",
                                                    "Falta el WebView2 Runtime de Microsoft.");
            return;
        }
        impl->environment->CreateCoreWebView2Controller (impl->accountWindow, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler> (
            [this, alive, ready] (HRESULT hr, ICoreWebView2Controller* c) -> HRESULT
            {
                if (alive.expired() || ! *alive.lock() || impl->accountWindow == nullptr)
                    return S_OK;
                if (FAILED (hr) || c == nullptr)
                {
                    closeAccount();
                    return S_OK;
                }
                impl->accountController = c;
                impl->accountController->get_CoreWebView2 (&impl->accountView);
                RECT rc {};
                GetClientRect (impl->accountWindow, &rc);
                impl->accountController->put_Bounds (rc);
                // Las ventanas que abra Google (ayuda, crear cuenta...) en la misma.
                impl->accountView->add_NewWindowRequested (Callback<ICoreWebView2NewWindowRequestedEventHandler> (
                    [this] (ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT
                    {
                        LPWSTR uri = nullptr;
                        args->put_Handled (TRUE);
                        if (SUCCEEDED (args->get_Uri (&uri)) && uri != nullptr && impl->accountView != nullptr)
                            impl->accountView->Navigate (uri);
                        CoTaskMemFree (uri);
                        return S_OK;
                    }).Get(), nullptr);
                // Cada pagina: mirar si ya hay sesion. Iniciada y de vuelta en YouTube: cerrar.
                impl->accountView->add_NavigationCompleted (Callback<ICoreWebView2NavigationCompletedEventHandler> (
                    [this, alive] (ICoreWebView2* view, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT
                    {
                        if (! alive.expired() && *alive.lock())
                            checkSignedIn (view);
                        return S_OK;
                    }).Get(), nullptr);
                ready();
                return S_OK;
            }).Get());
    });
}

void YouTubePlayer::showAccount()
{
    openAccountWindow (true, [this]
    {
        impl->signingIn = ! signedIn;
        impl->accountView->Navigate (signedIn ? L"https://www.youtube.com/account"
                                              : L"https://accounts.google.com/ServiceLogin?service=youtube&passive=true"
                                                L"&continue=https%3A%2F%2Fwww.youtube.com%2F");
    });
    sendChangeMessage();
}

void YouTubePlayer::signOut()
{
    // Sin pagina abierta, una ventana escondida solo para llegar a las cookies del perfil.
    const bool hidden = impl->accountWindow == nullptr;
    openAccountWindow (false, [this, hidden]
    {
        ComPtr<ICoreWebView2_2> view;
        ComPtr<ICoreWebView2CookieManager> cookies;
        if (SUCCEEDED (impl->accountView.As (&view)) && SUCCEEDED (view->get_CookieManager (&cookies)))
            cookies->DeleteAllCookies();
        updateSignedIn (false);
        if (hidden)
            closeAccount();
        else
            impl->accountView->Navigate (L"https://www.youtube.com/");
    });
}

bool YouTubePlayer::isAccountOpen() const { return impl->accountWindow != nullptr && IsWindowVisible (impl->accountWindow); }

void YouTubePlayer::closeAccount()
{
    if (impl->accountController != nullptr)
        impl->accountController->Close();
    impl->accountController.Reset();
    impl->accountView.Reset();
    impl->signingIn = false;
    if (impl->accountWindow != nullptr)
        DestroyWindow (impl->accountWindow);
    impl->accountWindow = nullptr;
    sendChangeMessage();
}

void YouTubePlayer::checkSignedIn (void* webView)
{
    ComPtr<ICoreWebView2> view (static_cast<ICoreWebView2*> (webView));
    ComPtr<ICoreWebView2_2> view2;
    ComPtr<ICoreWebView2CookieManager> cookies;
    if (FAILED (view.As (&view2)) || FAILED (view2->get_CookieManager (&cookies)))
        return;
    LPWSTR source = nullptr;
    view->get_Source (&source);
    const juce::String url (source != nullptr ? source : L"");
    CoTaskMemFree (source);
    std::weak_ptr<bool> alive = impl->alive;
    cookies->GetCookies (L"https://www.youtube.com", Callback<ICoreWebView2GetCookiesCompletedHandler> (
        [this, alive, url] (HRESULT hr, ICoreWebView2CookieList* list) -> HRESULT
        {
            if (alive.expired() || ! *alive.lock() || FAILED (hr) || list == nullptr)
                return S_OK;
            // LOGIN_INFO: la sesion de YouTube (la pone YouTube al volver del inicio de sesion).
            bool found = false;
            UINT count = 0;
            list->get_Count (&count);
            for (UINT i = 0; i < count && ! found; ++i)
            {
                ComPtr<ICoreWebView2Cookie> cookie;
                LPWSTR name = nullptr;
                if (SUCCEEDED (list->GetValueAtIndex (i, &cookie)) && SUCCEEDED (cookie->get_Name (&name)) && name != nullptr)
                    found = juce::String (name) == "LOGIN_INFO";
                CoTaskMemFree (name);
            }
            updateSignedIn (found);
            if (found && impl->signingIn && url.startsWith ("https://www.youtube.com"))
                juce::MessageManager::callAsync ([this, alive] { if (! alive.expired() && *alive.lock()) closeAccount(); });
            return S_OK;
        }).Get());
}

void YouTubePlayer::updateSignedIn (bool now)
{
    if (signedIn != now)
    {
        signedIn = now;
        sendChangeMessage();
    }
}

void YouTubePlayer::handleMessage (const juce::String& json)
{
    const auto v = juce::JSON::parse (json);
    // La pagina de Google con el captcha (no es la del video): hay que resolverlo a mano.
    if (v.isObject() && (bool) v["captcha"] && status.state != State::closed)
    {
        status.error = {};
        setState (State::verify);
        return;
    }
    // Solo los avisos de la pagina del video pedido: los de la anterior siguen llegando un rato
    // (su "termino" saltaba otro video de la lista, y otro, hasta el ultimo).
    if (! v.isObject() || status.state == State::closed || v["id"].toString() != video.id + "|" + juce::String (impl->loadCount))
        return;
    const auto previous = status.state;
    const auto newState = stateFromText (v["state"].toString());
    status.time = (double) v["t"];
    if ((double) v["d"] > 0.0)
        status.duration = (double) v["d"];
    status.error = v["err"].toString();
    // Resuelta la verificacion (el video ya carga o suena): la ventana vuelve fuera de las pantallas.
    if (impl->verifying && (newState == State::playing || newState == State::ad))
        hideVerification();
    if (newState != previous)
    {
        status.state = newState;
        if (newState == State::playing || newState == State::ad || newState == State::opening)
        {
            send ("{\"cmd\":\"volume\",\"value\":" + juce::String (volume) + "}");
            sendSettings();
        }
    }
    sendChangeMessage();   // el tiempo cambia en cada aviso
    if (newState == State::ended && previous != State::ended && onEnded)
        onEnded();
}

void YouTubePlayer::send (const juce::String& json)
{
    if (impl->webView != nullptr)
        impl->webView->PostWebMessageAsJson (wide (json).c_str());
}

void YouTubePlayer::showVerification()
{
    if (impl->window == nullptr)
        return;
    impl->placeWindow (true, width, height);
    send ("{\"cmd\":\"verifying\",\"value\":true}");
    sendChangeMessage();
}

void YouTubePlayer::hideVerification()
{
    if (! impl->verifying)
        return;
    impl->placeWindow (false, width, height);
    send ("{\"cmd\":\"verifying\",\"value\":false}");
    sendChangeMessage();
}

bool YouTubePlayer::isVerifying() const { return impl->verifying; }

void YouTubePlayer::skipAd() { send ("{\"cmd\":\"skipAd\"}"); }

void YouTubePlayer::play()       { send ("{\"cmd\":\"play\"}"); }
void YouTubePlayer::pause()      { send ("{\"cmd\":\"pause\"}"); }
void YouTubePlayer::togglePlay() { status.state == State::playing ? pause() : play(); }
void YouTubePlayer::seek (double seconds) { send ("{\"cmd\":\"seek\",\"value\":" + juce::String (seconds) + "}"); }

void YouTubePlayer::setVolume (float v)
{
    volume = juce::jlimit (0.0f, 1.0f, v);
    send ("{\"cmd\":\"volume\",\"value\":" + juce::String (volume) + "}");
}

void YouTubePlayer::setQuality (const juce::String& q)
{
    quality = q;
    sendSettings();
}

void YouTubePlayer::setSkipAds (bool skip)
{
    skipAds = skip;
    sendSettings();
}

void YouTubePlayer::sendSettings()
{
    send ("{\"cmd\":\"settings\",\"quality\":\"" + quality + "\",\"skipAds\":" + (skipAds ? "true" : "false") + "}");
}

juce::StringArray YouTubePlayer::getQualities() { return { "auto", "medium", "large", "hd720", "hd1080" }; }

juce::String YouTubePlayer::qualityName (const juce::String& q)
{
    if (q == "medium") return "360p";
    if (q == "large")  return "480p";
    if (q == "hd720")  return "720p";
    if (q == "hd1080") return "1080p";
    return "Auto";
}

void YouTubePlayer::close()
{
    impl->pendingId.clear();
    impl->destroyWindow();
    video = {};
    status = {};
    sendChangeMessage();
}
}
