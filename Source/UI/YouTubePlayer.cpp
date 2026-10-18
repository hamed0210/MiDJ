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
    tp-yt-iron-overlay-backdrop, .ytp-cued-thumbnail-overlay { display: none !important; }
    #movie_player, #movie_player.ytp-fullscreen {
      position: fixed !important; inset: 0 !important; width: 100vw !important; height: 100vh !important;
      z-index: 2147483647 !important; background: #000 !important; }
    #movie_player .html5-video-container video {
      position: fixed !important; inset: 0 !important; width: 100vw !important; height: 100vh !important;
      object-fit: contain !important; left: 0 !important; top: 0 !important; }`;
  const addCss = () => {
    if (document.getElementById('midj-css') || !document.head) return;
    const s = document.createElement('style'); s.id = 'midj-css'; s.textContent = css; document.head.appendChild(s);
  };
  document.addEventListener('DOMContentLoaded', addCss);

  const post = (o) => { try { window.chrome.webview.postMessage(JSON.stringify(o)); } catch (e) {} };
  let wantedVolume = -1, appliedVolume = -1, qualityAsked = false, lastKick = 0, endedSent = false, userPaused = false;
  let stuckSince = 0, reloaded = sessionStorage.getItem('midjReloaded') === location.href;
  setInterval(() => {
    addCss();
    const p = document.getElementById('movie_player');
    const v = document.querySelector('#movie_player video');
    const ad = !!(p && p.classList.contains('ad-showing'));
    let state = 'opening';
    if (p && v) {
      const now = performance.now();
      // 720p: solo con el video ya sonando (pedirlo durante un anuncio deja el video sin cargar).
      if (!qualityAsked && !ad && v.currentTime > 1 && p.setPlaybackQualityRange) { qualityAsked = true; try { p.setPlaybackQualityRange('hd720', 'hd720'); } catch (e) {} }
      if (wantedVolume >= 0 && wantedVolume !== appliedVolume) { appliedVolume = wantedVolume; try { p.setVolume(Math.round(wantedVolume * 100)); p.unMute(); } catch (e) {} }
      // Parado al principio (sin autoplay, o tras los anuncios): pedirle play cada 4 s (salvo pausa de MiDJ).
      if (!ad && !userPaused && v.paused && v.currentTime < 0.5 && now - lastKick > 4000 && now > 4000) { lastKick = now; try { p.playVideo(); } catch (e) {} }
      // Atascado sin datos (tras un anuncio): a los 8 s, play otra vez; a los 16 s, recargar (una vez).
      const stuck = !ad && !userPaused && v.readyState < 2 && v.currentTime < 0.5;
      if (!stuck) stuckSince = 0;
      else if (stuckSince === 0) stuckSince = now;
      else if (now - stuckSince > 16000 && !reloaded) { reloaded = true; sessionStorage.setItem('midjReloaded', location.href); location.reload(); }
      else if (now - stuckSince > 8000 && now - lastKick > 4000) { lastKick = now; try { p.playVideo(); } catch (e) {} }
      const ended = !ad && (v.ended || (p.getPlayerState && p.getPlayerState() === 0));
      state = ad ? 'ad' : ended ? 'ended' : v.paused ? (v.currentTime > 0 ? 'paused' : 'opening') : 'playing';
      if (ended && !endedSent) { endedSent = true; try { p.pauseVideo(); } catch (e) {} }   // que YouTube no ponga el siguiente
    }
    const errEl = document.querySelector('.ytp-error-content-wrap-reason, yt-playability-error-supported-renderers #reason, .ytp-error');
    const err = errEl ? errEl.innerText.trim().slice(0, 200) : '';
    if (err) state = 'error';
    post({ state, err, t: v ? v.currentTime : 0, d: v && !ad && isFinite(v.duration) ? v.duration : 0 });
  }, 250);

  window.chrome.webview.addEventListener('message', (e) => {
    const p = document.getElementById('movie_player');
    const m = e.data;
    if (m.cmd === 'volume') { wantedVolume = m.value; return; }
    if (!p) return;
    try {
      if (m.cmd === 'play') { userPaused = false; p.playVideo(); }
      else if (m.cmd === 'pause') { userPaused = true; p.pauseVideo(); }
      else if (m.cmd === 'seek') p.seekTo(m.value, true);
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
    juce::String pendingId;   // el video a cargar en cuanto este listo
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
        done = true;
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

    void destroyWindow()
    {
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
}

YouTubePlayer::State YouTubePlayer::stateFromText (const juce::String& s)
{
    if (s == "ad")      return State::ad;
    if (s == "playing") return State::playing;
    if (s == "paused")  return State::paused;
    if (s == "ended")   return State::ended;
    if (s == "error")   return State::error;
    return State::opening;
}

juce::String YouTubePlayer::stateText (const Status& s)
{
    switch (s.state)
    {
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
    impl->createWindow (width, height);

    auto navigate = [this]
    {
        if (impl->webView == nullptr || impl->pendingId.isEmpty())
            return;
        impl->webView->Navigate (wide ("https://www.youtube.com/watch?v=" + impl->pendingId).c_str());
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
                navigate();
                return S_OK;
            }).Get());
    };

    if (impl->environment != nullptr)
    {
        createController();
        return;
    }
    if (impl->creatingEnvironment)
        return;
    impl->creatingEnvironment = true;
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    options->put_AdditionalBrowserArguments (browserArguments);
    const auto profile = dataFolder.getChildFile ("WebView2");
    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions (nullptr, wide (profile.getFullPathName()).c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler> (
            [this, alive, createController] (HRESULT result, ICoreWebView2Environment* env) -> HRESULT
            {
                if (alive.expired() || ! *alive.lock())
                    return S_OK;
                impl->creatingEnvironment = false;
                if (FAILED (result) || env == nullptr)
                {
                    status.error = "Falta el WebView2 Runtime de Microsoft";
                    setState (State::error);
                    return S_OK;
                }
                impl->environment = env;
                if (impl->window != nullptr)
                    createController();
                return S_OK;
            }).Get());
    if (FAILED (hr))
    {
        impl->creatingEnvironment = false;
        status.error = "Falta el WebView2 Runtime de Microsoft";
        setState (State::error);
    }
}

void YouTubePlayer::handleMessage (const juce::String& json)
{
    const auto v = juce::JSON::parse (json);
    if (! v.isObject() || status.state == State::closed)
        return;
    const auto previous = status.state;
    const auto newState = stateFromText (v["state"].toString());
    status.time = (double) v["t"];
    if ((double) v["d"] > 0.0)
        status.duration = (double) v["d"];
    status.error = v["err"].toString();
    if (newState != previous)
    {
        status.state = newState;
        if (newState == State::playing || newState == State::ad)
            send ("{\"cmd\":\"volume\",\"value\":" + juce::String (volume) + "}");
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

void YouTubePlayer::play()       { send ("{\"cmd\":\"play\"}"); }
void YouTubePlayer::pause()      { send ("{\"cmd\":\"pause\"}"); }
void YouTubePlayer::togglePlay() { status.state == State::playing ? pause() : play(); }
void YouTubePlayer::seek (double seconds) { send ("{\"cmd\":\"seek\",\"value\":" + juce::String (seconds) + "}"); }

void YouTubePlayer::setVolume (float v)
{
    volume = juce::jlimit (0.0f, 1.0f, v);
    send ("{\"cmd\":\"volume\",\"value\":" + juce::String (volume) + "}");
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
