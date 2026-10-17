#include "Video/Capture.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <thread>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "windowsapp.lib")

using Microsoft::WRL::ComPtr;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;

namespace midj
{
// --- Listas de pantallas y ventanas ------------------------------------------------------------

namespace ScreenCapture
{
    std::vector<Monitor> monitors()
    {
        std::vector<Monitor> list;
        EnumDisplayMonitors (nullptr, nullptr, [] (HMONITOR m, HDC, LPRECT, LPARAM data) -> BOOL
        {
            MONITORINFOEXW info {};
            info.cbSize = sizeof (info);
            if (GetMonitorInfoW (m, &info))
            {
                Monitor mon;
                mon.id = juce::String (info.szDevice);
                mon.bounds = { info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left,
                               info.rcMonitor.bottom - info.rcMonitor.top };
                mon.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
                reinterpret_cast<std::vector<Monitor>*> (data)->push_back (mon);
            }
            return TRUE;
        }, (LPARAM) &list);
        return list;
    }

    static juce::String exeOf (HWND hwnd)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId (hwnd, &pid);
        juce::String exe;
        if (HANDLE process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
        {
            wchar_t path[MAX_PATH] {};
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW (process, 0, path, &size))
                exe = juce::File (juce::String (path)).getFileName();
            CloseHandle (process);
        }
        return exe;
    }

    // Como OBS: ventanas visibles con titulo, ni herramientas, ni hijas, ni ocultas por Windows (cloaked).
    static bool capturable (HWND hwnd)
    {
        if (! IsWindowVisible (hwnd))
            return false;
        const auto style = GetWindowLongPtrW (hwnd, GWL_STYLE), exStyle = GetWindowLongPtrW (hwnd, GWL_EXSTYLE);
        if ((style & WS_CHILD) != 0 || (exStyle & WS_EX_TOOLWINDOW) != 0)
            return false;
        DWORD cloaked = 0;
        if (SUCCEEDED (DwmGetWindowAttribute (hwnd, DWMWA_CLOAKED, &cloaked, sizeof (cloaked))) && cloaked != 0)
            return false;
        RECT rc {};
        return GetClientRect (hwnd, &rc) && (rc.right > 0 || IsIconic (hwnd));
    }

    std::vector<Window> windows()
    {
        std::vector<Window> list;
        EnumWindows ([] (HWND hwnd, LPARAM data) -> BOOL
        {
            if (! capturable (hwnd))
                return TRUE;
            wchar_t title[512] {}, cls[256] {};
            InternalGetWindowText (hwnd, title, 512);   // no se queda esperando a un programa colgado
            if (title[0] == 0)
                return TRUE;
            GetClassNameW (hwnd, cls, 256);
            Window w;
            w.hwnd = hwnd;
            w.title = juce::String (title);
            w.className = juce::String (cls);
            w.exe = exeOf (hwnd);
            if (w.exe.equalsIgnoreCase ("explorer.exe") && (w.className == "Progman" || w.className == "WorkerW"))
                return TRUE;   // el escritorio
            reinterpret_cast<std::vector<Window>*> (data)->push_back (w);
            return TRUE;
        }, (LPARAM) &list);
        return list;
    }

    bool windowCaptureSupported()
    {
        try
        {
            return wgc::GraphicsCaptureSession::IsSupported();
        }
        catch (...)
        {
            return false;
        }
    }

    juce::String describe (const Monitor& m, int index)
    {
        return "Pantalla " + juce::String (index + 1) + ": " + juce::String (m.bounds.getWidth()) + "x"
               + juce::String (m.bounds.getHeight()) + (m.primary ? " (principal)" : "");
    }

    juce::String describe (const Window& w)
    {
        return "[" + w.exe + "] " + w.title;
    }

    int findMonitor (const std::vector<Monitor>& list, const juce::String& id, int index)
    {
        for (size_t i = 0; i < list.size(); ++i)
            if (id.isNotEmpty() && list[i].id == id)
                return (int) i;
        return juce::isPositiveAndBelow (index, (int) list.size()) ? index : -1;
    }

    void* findWindow (const juce::String& title, const juce::String& className, const juce::String& exe)
    {
        if (title.isEmpty() && className.isEmpty() && exe.isEmpty())
            return nullptr;
        void* sameTitle = nullptr;
        void* sameClass = nullptr;
        void* sameExe = nullptr;
        for (const auto& w : windows())
        {
            if (! w.exe.equalsIgnoreCase (exe))
                continue;
            if (w.className == className && w.title == title)
                return w.hwnd;
            if (w.title == title && sameTitle == nullptr)
                sameTitle = w.hwnd;
            if (w.className == className && sameClass == nullptr)
                sameClass = w.hwnd;
            if (sameExe == nullptr)
                sameExe = w.hwnd;
        }
        if (sameTitle != nullptr) return sameTitle;   // la clase cambia en cada arranque en algunos programas (JUCE)
        if (sameClass != nullptr) return sameClass;
        return sameExe;
    }
}

// --- Texturas ------------------------------------------------------------------------------------

namespace
{
    struct Texture
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
        int width = 0, height = 0;

        // La crea si no existe o cambio el tamano.
        bool ensure (ID3D11Device* device, int w, int h)
        {
            if (texture != nullptr && w == width && h == height)
                return true;
            *this = {};
            D3D11_TEXTURE2D_DESC td {};
            td.Width = (UINT) w;
            td.Height = (UINT) h;
            td.MipLevels = td.ArraySize = 1;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED (device->CreateTexture2D (&td, nullptr, &texture))
                || FAILED (device->CreateShaderResourceView (texture.Get(), nullptr, &view)))
            {
                *this = {};
                return false;
            }
            width = w;
            height = h;
            return true;
        }
    };

    // El puntero del raton como imagen (para la captura de pantalla de DXGI, que no lo trae).
    // Se dibuja con DrawIconEx sobre negro y sobre blanco: de la diferencia sale el alfa, asi
    // valen todos los tipos de puntero (de color, con alfa o en blanco y negro). Las partes que
    // invierten lo de debajo (el cursor de texto) se hacen blancas con borde negro.
    struct CursorImage
    {
        HCURSOR current = nullptr;
        ComPtr<ID3D11ShaderResourceView> view;
        int width = 0, height = 0;
        POINT hotspot {};

        bool update (ID3D11Device* device, juce::Point<int> origin, Compositor::GpuSource::Picture& pic)
        {
            CURSORINFO ci {};
            ci.cbSize = sizeof (ci);
            if (! GetCursorInfo (&ci) || (ci.flags & CURSOR_SHOWING) == 0 || ci.hCursor == nullptr)
                return false;
            if (ci.hCursor != current)
            {
                current = ci.hCursor;
                rebuild (device);
            }
            if (view == nullptr)
                return false;
            pic.cursorView = view.Get();
            pic.cursor = { (float) (ci.ptScreenPos.x - hotspot.x - origin.x), (float) (ci.ptScreenPos.y - hotspot.y - origin.y),
                           (float) width, (float) height };
            return true;
        }

        void rebuild (ID3D11Device* device)
        {
            view.Reset();
            ICONINFO ii {};
            if (! GetIconInfo (current, &ii))
                return;
            BITMAP bm {};
            const bool colour = ii.hbmColor != nullptr;
            GetObject (colour ? ii.hbmColor : ii.hbmMask, sizeof (bm), &bm);
            if (ii.hbmColor != nullptr) DeleteObject (ii.hbmColor);
            if (ii.hbmMask != nullptr)  DeleteObject (ii.hbmMask);
            const int w = bm.bmWidth, h = colour ? bm.bmHeight : bm.bmHeight / 2;
            if (w <= 0 || h <= 0 || w > 256 || h > 256)
                return;
            hotspot = { (LONG) ii.xHotspot, (LONG) ii.yHotspot };

            auto drawOn = [&] (uint32_t background, std::vector<uint32_t>& pixels)
            {
                BITMAPINFO bi {};
                bi.bmiHeader.biSize = sizeof (bi.bmiHeader);
                bi.bmiHeader.biWidth = w;
                bi.bmiHeader.biHeight = -h;   // de arriba abajo
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                void* bits = nullptr;
                HDC dc = CreateCompatibleDC (nullptr);
                HBITMAP dib = CreateDIBSection (dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
                if (dib != nullptr && bits != nullptr)
                {
                    auto* p = static_cast<uint32_t*> (bits);
                    std::fill (p, p + w * h, background);
                    auto old = SelectObject (dc, dib);
                    DrawIconEx (dc, 0, 0, current, w, h, 0, nullptr, DI_NORMAL);
                    GdiFlush();
                    pixels.assign (p, p + w * h);
                    SelectObject (dc, old);
                }
                if (dib != nullptr) DeleteObject (dib);
                DeleteDC (dc);
            };
            std::vector<uint32_t> onBlack, onWhite;
            drawOn (0x00000000u, onBlack);
            drawOn (0x00ffffffu, onWhite);
            if (onBlack.size() != (size_t) (w * h) || onWhite.size() != onBlack.size())
                return;

            std::vector<uint32_t> out ((size_t) (w * h), 0u);
            std::vector<bool> invert ((size_t) (w * h), false);
            for (size_t i = 0; i < out.size(); ++i)
            {
                int diff = 0, inverted = 0;
                for (int shift = 0; shift < 24; shift += 8)
                {
                    const int b = (int) ((onBlack[i] >> shift) & 0xff), wh = (int) ((onWhite[i] >> shift) & 0xff);
                    diff = juce::jmax (diff, wh - b);
                    inverted += (b - wh > 128) ? 1 : 0;
                }
                if (inverted == 3)
                {
                    invert[i] = true;
                    out[i] = 0xffffffffu;
                    continue;
                }
                const uint32_t alpha = (uint32_t) juce::jlimit (0, 255, 255 - diff);
                uint32_t c = alpha << 24;
                for (int shift = 0; shift < 24; shift += 8)   // sobre negro ya es el color premultiplicado
                    c |= juce::jmin (alpha, (onBlack[i] >> shift) & 0xffu) << shift;
                out[i] = c;
            }
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    auto& px = out[(size_t) (y * w + x)];
                    if ((px >> 24) != 0)
                        continue;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            if (juce::isPositiveAndBelow (x + dx, w) && juce::isPositiveAndBelow (y + dy, h)
                                && invert[(size_t) ((y + dy) * w + x + dx)])
                                px = 0xff000000u;
                }

            D3D11_TEXTURE2D_DESC td {};
            td.Width = (UINT) w;
            td.Height = (UINT) h;
            td.MipLevels = td.ArraySize = 1;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA init { out.data(), (UINT) (w * 4), 0 };
            ComPtr<ID3D11Texture2D> texture;
            if (SUCCEEDED (device->CreateTexture2D (&td, &init, &texture)))
                device->CreateShaderResourceView (texture.Get(), nullptr, &view);
            width = w;
            height = h;
        }
    };

    // La zona de cliente (sin bordes ni barra de titulo) dentro de la imagen de una ventana, como OBS.
    bool clientBox (HWND window, UINT width, UINT height, D3D11_BOX& box)
    {
        RECT client {}, frame {};
        POINT topLeft {};
        if (IsIconic (window) || ! GetClientRect (window, &client) || client.right <= 0 || client.bottom <= 0
            || DwmGetWindowAttribute (window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof (frame)) != S_OK
            || ! ClientToScreen (window, &topLeft))
            return false;
        const UINT left = topLeft.x > frame.left ? (UINT) (topLeft.x - frame.left) : 0;
        const UINT top = topLeft.y > frame.top ? (UINT) (topLeft.y - frame.top) : 0;
        box.left = left;
        box.top = top;
        box.right = left + (width > left ? juce::jmin (width - left, (UINT) client.right) : 1);
        box.bottom = top + (height > top ? juce::jmin (height - top, (UINT) client.bottom) : 1);
        box.front = 0;
        box.back = 1;
        return box.right <= width && box.bottom <= height;
    }

    // Ejecuta algo en un hilo MTA de COM (este mismo si ya lo es).
    template <typename Fn>
    void inMta (Fn&& fn)
    {
        APTTYPE type {};
        APTTYPEQUALIFIER qualifier {};
        if (SUCCEEDED (CoGetApartmentType (&type, &qualifier)) && type == APTTYPE_MTA)
        {
            fn();
            return;
        }
        std::thread worker ([&]
        {
            const HRESULT hr = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
            fn();
            if (SUCCEEDED (hr))
                CoUninitialize();
        });
        worker.join();
    }

    template <typename T>
    ComPtr<T> dxgiInterfaceFrom (const winrt::Windows::Foundation::IInspectable& object)
    {
        ComPtr<T> result;
        auto access = object.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        access->GetInterface (__uuidof (T), reinterpret_cast<void**> (result.GetAddressOf()));
        return result;
    }
}

// --- Las dos formas de capturar ------------------------------------------------------------------

struct Capture::Backend
{
    virtual ~Backend() = default;
    virtual Picture update (ID3D11Device* device, ID3D11DeviceContext* context) = 0;
    virtual bool isLost() const = 0;
    juce::Point<int> size;   // de la ultima imagen (o la esperada antes de la primera)
    const char* method = "";
    juce::String note;   // por que no se uso la otra forma (para el panel)
};

namespace
{
    // Duplicacion del escritorio de DXGI (como el "monitor capture" de OBS en Windows 10).
    struct DuplicationBackend : Capture::Backend
    {
        ComPtr<IDXGIOutputDuplication> duplication;
        Texture image;
        bool hasImage = false, lost = false, cursor = true;
        juce::Point<int> origin;
        CursorImage pointer;

        bool start (ID3D11Device* device, HMONITOR monitor, bool withCursor)
        {
            method = "DXGI";
            cursor = withCursor;
            ComPtr<IDXGIDevice> dxgiDevice;
            ComPtr<IDXGIAdapter> adapter;
            if (FAILED (device->QueryInterface (IID_PPV_ARGS (&dxgiDevice))) || FAILED (dxgiDevice->GetAdapter (&adapter)))
                return false;
            ComPtr<IDXGIOutput> output;
            for (UINT i = 0; adapter->EnumOutputs (i, &output) != DXGI_ERROR_NOT_FOUND; ++i, output.Reset())
            {
                DXGI_OUTPUT_DESC desc {};
                if (FAILED (output->GetDesc (&desc)) || desc.Monitor != monitor)
                    continue;
                ComPtr<IDXGIOutput1> output1;
                // Como OBS: DuplicateOutput1 si se puede (en un programa con DPI por monitor v2, como
                // MiDJ, el DuplicateOutput de siempre falla con E_INVALIDARG).
                HRESULT hr = E_NOINTERFACE;
                ComPtr<IDXGIOutput5> output5;
                if (SUCCEEDED (output.As (&output5)))
                {
                    const DXGI_FORMAT formats[] = { DXGI_FORMAT_B8G8R8A8_UNORM };
                    hr = output5->DuplicateOutput1 (device, 0, 1, formats, &duplication);
                }
                if (FAILED (hr) && SUCCEEDED (output.As (&output1)))
                    hr = output1->DuplicateOutput (device, &duplication);
                if (FAILED (hr))
                {
                    note = "DXGI 0x" + juce::String::toHexString ((int) hr);
                    return false;
                }
                DXGI_OUTDUPL_DESC dd {};
                duplication->GetDesc (&dd);
                if (dd.Rotation != DXGI_MODE_ROTATION_IDENTITY && dd.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED)
                {
                    duplication.Reset();   // girada: mejor Windows Graphics Capture, que la endereza
                    note = "pantalla girada";
                    return false;
                }
                origin = { desc.DesktopCoordinates.left, desc.DesktopCoordinates.top };
                size = { (int) dd.ModeDesc.Width, (int) dd.ModeDesc.Height };
                return true;
            }
            note = "pantalla de otra tarjeta";
            return false;   // la pantalla esta en otra tarjeta grafica
        }

        Capture::Picture update (ID3D11Device* device, ID3D11DeviceContext* context) override
        {
            Capture::Picture pic;
            if (duplication != nullptr && ! lost)
            {
                DXGI_OUTDUPL_FRAME_INFO info {};
                ComPtr<IDXGIResource> resource;
                const HRESULT hr = duplication->AcquireNextFrame (0, &info, &resource);
                if (SUCCEEDED (hr))
                {
                    ComPtr<ID3D11Texture2D> desktop;
                    if ((info.LastPresentTime.QuadPart != 0 || ! hasImage) && SUCCEEDED (resource.As (&desktop)))
                    {
                        D3D11_TEXTURE2D_DESC desc {};
                        desktop->GetDesc (&desc);
                        if (image.ensure (device, (int) desc.Width, (int) desc.Height))
                        {
                            context->CopyResource (image.texture.Get(), desktop.Get());
                            hasImage = true;
                            size = { image.width, image.height };
                        }
                    }
                    duplication->ReleaseFrame();
                }
                else if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_INVALID_CALL)
                {
                    // Cambio de resolucion, pantalla de UAC...: se suelta ya (solo puede haber una
                    // duplicacion por pantalla, y la nueva no podria crearse) y se vuelve a crear.
                    // Mientras, se sigue mostrando la ultima imagen.
                    lost = true;
                    duplication.Reset();
                }
            }
            if (! hasImage)
                return pic;
            pic.view = image.view.Get();
            pic.width = image.width;
            pic.height = image.height;
            pic.opaque = true;
            if (cursor)
                pointer.update (device, origin, pic);
            return pic;
        }

        bool isLost() const override { return lost; }
    };

    // Windows Graphics Capture: ventanas (y pantallas de otra tarjeta). Los fotogramas se recogen
    // en el hilo del compositor (grupo de fotogramas "free threaded"): se toma el ultimo y se copia.
    struct GraphicsCaptureBackend : Capture::Backend
    {
        wgc::GraphicsCaptureItem item { nullptr };
        wgc::Direct3D11CaptureFramePool pool { nullptr };
        wgc::GraphicsCaptureSession session { nullptr };
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrtDevice { nullptr };
        winrt::event_token closedToken {};
        std::atomic<bool> closed { false };
        winrt::Windows::Graphics::SizeInt32 poolSize {};
        HWND window = nullptr;
        bool clientArea = true;
        bool cursor = true;
        Texture image;
        bool hasImage = false;

        ~GraphicsCaptureBackend() override
        {
            inMta ([this]
            {
                try
                {
                    if (item != nullptr)
                        item.Closed (closedToken);
                    if (session != nullptr) session.Close();
                    if (pool != nullptr)    pool.Close();
                }
                catch (...) {}
                session = nullptr;
                pool = nullptr;
                item = nullptr;
                winrtDevice = nullptr;
            });
        }

        // Se crea en un hilo MTA: asi el hilo del compositor (MTA) puede usarla; creada en el hilo
        // de la interfaz (STA) da RPC_E_WRONG_THREAD al rehacer el grupo de fotogramas.
        bool start (ID3D11Device* device, HWND hwnd, HMONITOR monitor, bool withCursor, bool client)
        {
            bool ok = false;
            inMta ([&] { ok = startHere (device, hwnd, monitor, withCursor, client); });
            return ok;
        }

        bool startHere (ID3D11Device* device, HWND hwnd, HMONITOR monitor, bool withCursor, bool client)
        {
            method = "WGC";
            cursor = withCursor;
            window = hwnd;
            clientArea = client && hwnd != nullptr;
            try
            {
                if (! wgc::GraphicsCaptureSession::IsSupported())
                    return false;
                auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
                HRESULT hr = hwnd != nullptr
                                 ? interop->CreateForWindow (hwnd, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi (item))
                                 : interop->CreateForMonitor (monitor, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi (item));
                if (FAILED (hr) || item == nullptr)
                    return false;

                ComPtr<IDXGIDevice> dxgiDevice;
                if (FAILED (device->QueryInterface (IID_PPV_ARGS (&dxgiDevice))))
                    return false;
                winrt::com_ptr<::IInspectable> inspectable;
                if (FAILED (CreateDirect3D11DeviceFromDXGIDevice (dxgiDevice.Get(), inspectable.put())))
                    return false;
                winrtDevice = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

                poolSize = item.Size();
                pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded (winrtDevice, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                                                            2, poolSize);
                session = pool.CreateCaptureSession (item);
                try { session.IsCursorCaptureEnabled (cursor); } catch (...) {}   // Windows 10 2004+
                try { session.IsBorderRequired (false); } catch (...) {}          // Windows 11: sin borde amarillo
                closedToken = item.Closed ([this] (auto&&, auto&&) { closed = true; });
                session.StartCapture();
                size = { poolSize.Width, poolSize.Height };
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        Capture::Picture update (ID3D11Device* device, ID3D11DeviceContext* context) override
        {
            Capture::Picture pic;
            const char* step = "TryGetNextFrame";
            try
            {
                wgc::Direct3D11CaptureFrame latest { nullptr };
                while (auto frame = pool.TryGetNextFrame())   // si hay varios, el ultimo
                {
                    if (latest != nullptr)
                        latest.Close();
                    latest = frame;
                }
                if (latest != nullptr)
                {
                    step = "Surface";
                    auto surface = dxgiInterfaceFrom<ID3D11Texture2D> (latest.Surface());
                    step = "ContentSize";
                    D3D11_TEXTURE2D_DESC desc {};
                    if (surface != nullptr)
                        surface->GetDesc (&desc);
                    const auto content = latest.ContentSize();
                    // La textura del grupo puede ser mas grande que la ventana: solo vale lo de ContentSize.
                    D3D11_BOX box { 0, 0, 0, (UINT) juce::jmin ((int) desc.Width, content.Width),
                                    (UINT) juce::jmin ((int) desc.Height, content.Height), 1 };
                    bool ok = surface != nullptr && box.right > 0 && box.bottom > 0;
                    if (ok && clientArea)
                        ok = clientBox (window, box.right, box.bottom, box);
                    if (ok && image.ensure (device, (int) (box.right - box.left), (int) (box.bottom - box.top)))
                    {
                        context->CopySubresourceRegion (image.texture.Get(), 0, 0, 0, 0, surface.Get(), 0, &box);
                        hasImage = true;
                        size = { image.width, image.height };
                    }
                    step = "Close";
                    latest.Close();
                    if (content.Width != poolSize.Width || content.Height != poolSize.Height)
                    {
                        // La ventana cambio de tamano: el grupo se rehace a la nueva medida.
                        step = "Recreate";
                        poolSize = content;
                        pool.Recreate (winrtDevice, wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, poolSize);
                    }
                }
            }
            catch (const winrt::hresult_error& e)
            {
                closed = true;
                note = juce::String (step) + " 0x" + juce::String::toHexString ((int) e.code().value);
            }
            catch (...)
            {
                closed = true;
                note = "error";
            }
            if (! hasImage)
                return pic;
            pic.view = image.view.Get();
            pic.width = image.width;
            pic.height = image.height;
            pic.opaque = true;
            return pic;
        }

        bool isLost() const override { return closed.load() || (window != nullptr && ! IsWindow (window)); }
    };
}

// --- Captura -------------------------------------------------------------------------------------

juce::String Capture::keyFor (const SceneItem& item)
{
    if (item.type == SourceType::screen)
        return "screen|" + item.monitorId + "|" + juce::String (item.monitor) + "|" + juce::String ((int) item.cursor);
    return "window|" + item.windowTitle + "|" + item.windowClass + "|" + item.windowExe + "|" + juce::String ((int) item.cursor)
           + juce::String ((int) item.clientArea);
}

Capture::Capture (void* d, const SceneItem& i) : device (d), item (i), key (keyFor (i))
{
    reconnect();
}

Capture::~Capture() = default;

bool Capture::reconnect()
{
    const auto now = juce::Time::getMillisecondCounter();
    // Pantalla capturada con WGC porque DXGI fallo (puede que solo un momento): cada 10 s se
    // vuelve a probar DXGI, que no pinta el borde amarillo y cuesta menos.
    bool upgrade = false;
    {
        const std::lock_guard<std::mutex> sl (lock);
        if (backend != nullptr && ! backend->isLost())
        {
            if (! retryDxgi || now - lastDxgiTry < 10000)
                return true;
            upgrade = true;
        }
    }
    auto* d3d = static_cast<ID3D11Device*> (device);
    std::unique_ptr<Backend> fresh;
    juce::String problem;
    if (d3d == nullptr)
    {
        problem = "Sin Direct3D";
    }
    else if (item.type == SourceType::screen)
    {
        const auto list = ScreenCapture::monitors();
        const int index = ScreenCapture::findMonitor (list, item.monitorId, item.monitor);
        if (index < 0)
        {
            if (upgrade)
                return true;
            problem = "No esta esa pantalla";
        }
        else
        {
            const auto& b = list[(size_t) index].bounds;
            if (width.load() == 0)
            {
                width = b.getWidth();   // el tamano esperado, antes de la primera imagen
                height = b.getHeight();
            }
            const POINT centre { b.getCentreX(), b.getCentreY() };
            HMONITOR monitor = MonitorFromPoint (centre, MONITOR_DEFAULTTONULL);
            auto dup = std::make_unique<DuplicationBackend>();
            if (dup->start (d3d, monitor, item.cursor))
            {
                fresh = std::move (dup);
                dxgiFailures = 0;
                retryDxgi = false;
            }
            else if (upgrade)
            {
                lastDxgiTry = now;   // sigue con WGC
                return true;
            }
            else if (dup->note.startsWith ("DXGI") && ++dxgiFailures < 4)
            {
                // Puede que otra captura de esta pantalla aun no se haya soltado (solo se puede
                // duplicar una vez): se reintenta en el siguiente tick antes de pasar a WGC.
                problem = "Iniciando la captura...";
            }
            else
            {
                auto wgcBackend = std::make_unique<GraphicsCaptureBackend>();
                wgcBackend->note = dup->note;
                if (wgcBackend->start (d3d, nullptr, monitor, item.cursor, false))
                {
                    fresh = std::move (wgcBackend);
                    // Pantalla girada o de otra tarjeta: DXGI no va a funcionar despues tampoco.
                    retryDxgi = dup->note.startsWith ("DXGI");
                    lastDxgiTry = now;
                }
                else
                    problem = "No se pudo capturar la pantalla";
            }
        }
    }
    else
    {
        auto* hwnd = (HWND) ScreenCapture::findWindow (item.windowTitle, item.windowClass, item.windowExe);
        if (hwnd == nullptr)
        {
            problem = item.windowTitle.isEmpty() ? "Elige una ventana" : "La ventana no esta abierta";
        }
        else
        {
            auto wgcBackend = std::make_unique<GraphicsCaptureBackend>();
            if (wgcBackend->start (d3d, hwnd, nullptr, item.cursor, item.clientArea))
                fresh = std::move (wgcBackend);
            else
                problem = ScreenCapture::windowCaptureSupported() ? "No se pudo capturar la ventana"
                                                                  : "Windows no permite capturar ventanas (hace falta Windows 10 1903)";
        }
    }

    const std::lock_guard<std::mutex> sl (lock);
    status = problem;
    if (fresh != nullptr)
    {
        if (width.load() == 0)
        {
            width = fresh->size.x;
            height = fresh->size.y;
        }
        // La vieja se suelta en el hilo del compositor al empezar el siguiente fotograma: puede
        // que este dibujando ahora mismo con su textura.
        if (backend != nullptr)
            retired.push_back (std::move (backend));
        backend = std::move (fresh);
        return true;
    }
    return false;
}

Capture::Picture Capture::update (void* d, void* context)
{
    const std::lock_guard<std::mutex> sl (lock);
    retired.clear();
    if (backend == nullptr)
        return {};
    auto pic = backend->update (static_cast<ID3D11Device*> (d), static_cast<ID3D11DeviceContext*> (context));
    if (pic.view != nullptr)
    {
        width = pic.width;
        height = pic.height;
    }
    return pic;
}

juce::Point<int> Capture::getSize() const
{
    return { width.load(), height.load() };
}

juce::String Capture::getMethod() const
{
    const std::lock_guard<std::mutex> sl (lock);
    if (backend == nullptr)
        return {};
    return backend->note.isEmpty() ? juce::String (backend->method) : juce::String (backend->method) + ", " + backend->note;
}

juce::String Capture::getStatus() const
{
    const std::lock_guard<std::mutex> sl (lock);
    if (status.isEmpty() && backend != nullptr && backend->isLost())
        return "Se perdio la captura; reintentando (" + juce::String (backend->method) + (backend->note.isNotEmpty() ? ", " + backend->note : juce::String()) + ")";
    return status;
}

// --- Gestor --------------------------------------------------------------------------------------

CaptureManager::CaptureManager (void* d) : device (d)
{
}

std::shared_ptr<Capture> CaptureManager::get (const SceneItem& item)
{
    const auto key = Capture::keyFor (item);
    auto& c = captures[item.id];
    if (c != nullptr && c->getKey() == key)
        return c;
    c.reset();
    // Dos elementos que capturan lo mismo comparten la captura (como una fuente de OBS en dos escenas).
    for (const auto& [id, other] : captures)
        if (other != nullptr && other->getKey() == key)
            return c = other;
    return c = std::make_shared<Capture> (device, item);
}

std::shared_ptr<Capture> CaptureManager::find (int itemId) const
{
    if (auto it = captures.find (itemId); it != captures.end())
        return it->second;
    return nullptr;
}

void CaptureManager::keepOnly (const std::set<int>& itemIds)
{
    for (auto it = captures.begin(); it != captures.end();)
        it = itemIds.count (it->first) > 0 ? std::next (it) : captures.erase (it);
}

void CaptureManager::tick()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastTick < 1000)
        return;
    lastTick = now;
    for (auto& [id, c] : captures)
        if (c != nullptr)
            c->reconnect();
}
}
