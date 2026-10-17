#include "Video/Compositor.h"

#include <juce_core/juce_core.h>

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <map>
#include <mutex>
#include <thread>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "winmm.lib")

using Microsoft::WRL::ComPtr;

namespace midj
{
namespace
{
    // Un solo vertex shader: un rectangulo (4 vertices sin bufer) colocado por las constantes.
    // Dos pixel shaders:
    //  - capa: las imagenes de JUCE son premultiplicadas en espacio sRGB; se pasan a luz lineal
    //    (como OBS, que mezcla en lineal: un fundido a la mitad no oscurece) y se aplica la opacidad.
    //    El programa es una textura sRGB: la GPU mezcla en lineal y guarda en sRGB.
    //  - copia: el programa a una salida (las dos vistas sRGB: el filtrado es en lineal).
    const char* shaderSource = R"(
cbuffer Placement : register(b0)
{
    float4 dest;     // izquierda, arriba, derecha, abajo en coordenadas de pantalla (-1..1)
    float4 uv;       // zona de la textura
    float4 params;   // x = opacidad
};
Texture2D image : register(t0);
SamplerState linearClamp : register(s0);

struct VsOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VsOut vs (uint id : SV_VertexID)
{
    float2 corner = float2 (id & 1, id >> 1);   // (0,0) (1,0) (0,1) (1,1): tira de 2 triangulos
    VsOut o;
    o.pos = float4 (lerp (dest.x, dest.z, corner.x), lerp (dest.y, dest.w, corner.y), 0, 1);
    o.uv = lerp (uv.xy, uv.zw, corner);
    return o;
}

float3 srgbToLinear (float3 c)
{
    return c <= 0.04045 ? c / 12.92 : pow ((c + 0.055) / 1.055, 2.4);
}

float4 psLayer (VsOut i) : SV_Target
{
    float4 c = image.Sample (linearClamp, i.uv);
    float3 straight = c.a > 0 ? c.rgb / c.a : 0;
    float a = c.a * params.x;
    return float4 (srgbToLinear (saturate (straight)) * a, a);
}

float4 psCopy (VsOut i) : SV_Target
{
    return image.Sample (linearClamp, i.uv);
}
)";

    struct Placement
    {
        float dest[4];
        float uv[4];
        float params[4];
    };

    struct Output
    {
        HWND hwnd = nullptr;
        ComPtr<IDXGISwapChain1> swapChain;
        ComPtr<ID3D11RenderTargetView> target;
        int width = 0, height = 0;
        bool failed = false;
    };

    struct SlotTexture
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
        int width = 0, height = 0;
        juce::uint64 version = 0;
        bool used = false;
    };
}

struct Compositor::Impl
{
    Settings settings;
    juce::String error;
    bool hardware = false;
    juce::String adapter;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIFactory2> factory;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> layerShader, copyShader;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11BlendState> premultipliedBlend;
    ComPtr<ID3D11RasterizerState> rasterizer;

    ComPtr<ID3D11Texture2D> program, staging;
    ComPtr<ID3D11RenderTargetView> programTarget;
    ComPtr<ID3D11ShaderResourceView> programView;

    std::map<int, SlotTexture> slots;
    std::mutex gpuLock;   // el contexto de Direct3D se usa desde un hilo cada vez

    std::mutex outputsLock;
    std::vector<Output> outputs;

    std::thread thread;
    std::atomic<bool> running { false };
    std::function<void (std::vector<Layer>&)>* build = nullptr;

    std::atomic<juce::uint64> frames { 0 }, lagged { 0 };
    std::atomic<double> renderMs { 0.0 };

    bool create()
    {
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = E_FAIL;
        if (settings.allowHardware)
            hr = D3D11CreateDevice (nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, (UINT) std::size (levels),
                                    D3D11_SDK_VERSION, &device, nullptr, &context);
        hardware = SUCCEEDED (hr);
        if (! hardware)
            hr = D3D11CreateDevice (nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, (UINT) std::size (levels),
                                    D3D11_SDK_VERSION, &device, nullptr, &context);
        if (FAILED (hr))
            return fail ("No se pudo crear Direct3D 11", hr);

        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> dxgiAdapter;
        if (FAILED (device.As (&dxgiDevice)) || FAILED (dxgiDevice->GetAdapter (&dxgiAdapter))
            || FAILED (dxgiAdapter->GetParent (IID_PPV_ARGS (&factory))))
            return fail ("Sin DXGI 1.2", E_FAIL);
        DXGI_ADAPTER_DESC desc {};
        if (SUCCEEDED (dxgiAdapter->GetDesc (&desc)))
            adapter = juce::String (desc.Description).trim();

        if (! compileShaders())
            return false;

        D3D11_BUFFER_DESC cb {};
        cb.ByteWidth = sizeof (Placement);
        cb.Usage = D3D11_USAGE_DEFAULT;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED (hr = device->CreateBuffer (&cb, nullptr, &constants)))
            return fail ("CreateBuffer", hr);

        D3D11_SAMPLER_DESC sd {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED (hr = device->CreateSamplerState (&sd, &sampler)))
            return fail ("CreateSamplerState", hr);

        D3D11_BLEND_DESC bd {};
        auto& rt = bd.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;            // premultiplicado: encima + debajo * (1 - alfa)
        rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED (hr = device->CreateBlendState (&bd, &premultipliedBlend)))
            return fail ("CreateBlendState", hr);

        D3D11_RASTERIZER_DESC rd {};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        if (FAILED (hr = device->CreateRasterizerState (&rd, &rasterizer)))
            return fail ("CreateRasterizerState", hr);

        // El programa: sin tipo para poder verlo como sRGB (mezcla en lineal) y copiarlo a la CPU.
        D3D11_TEXTURE2D_DESC td {};
        td.Width = (UINT) settings.width;
        td.Height = (UINT) settings.height;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED (hr = device->CreateTexture2D (&td, nullptr, &program)))
            return fail ("Textura del programa", hr);
        D3D11_RENDER_TARGET_VIEW_DESC rtv {};
        rtv.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv {};
        srv.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        if (FAILED (hr = device->CreateRenderTargetView (program.Get(), &rtv, &programTarget))
            || FAILED (hr = device->CreateShaderResourceView (program.Get(), &srv, &programView)))
            return fail ("Vistas del programa", hr);
        const float black[4] = { 0, 0, 0, 1 };
        context->ClearRenderTargetView (programTarget.Get(), black);
        return true;
    }

    bool fail (const juce::String& what, HRESULT hr)
    {
        error = what + " (0x" + juce::String::toHexString ((int) hr) + ")";
        device.Reset();
        context.Reset();
        return false;
    }

    bool compileShaders()
    {
        auto compile = [this] (const char* entry, const char* target, ComPtr<ID3DBlob>& code)
        {
            ComPtr<ID3DBlob> errors;
            const HRESULT hr = D3DCompile (shaderSource, strlen (shaderSource), "compositor", nullptr, nullptr, entry, target,
                                           D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
            if (FAILED (hr))
            {
                error = juce::String ("Shader ") + entry + ": "
                        + (errors != nullptr ? juce::String ((const char*) errors->GetBufferPointer(), errors->GetBufferSize()) : juce::String());
                return false;
            }
            return true;
        };
        ComPtr<ID3DBlob> vs, layer, copy;
        if (! compile ("vs", "vs_4_0", vs) || ! compile ("psLayer", "ps_4_0", layer) || ! compile ("psCopy", "ps_4_0", copy))
        {
            device.Reset();
            return false;
        }
        if (FAILED (device->CreateVertexShader (vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertexShader))
            || FAILED (device->CreatePixelShader (layer->GetBufferPointer(), layer->GetBufferSize(), nullptr, &layerShader))
            || FAILED (device->CreatePixelShader (copy->GetBufferPointer(), copy->GetBufferSize(), nullptr, &copyShader)))
            return fail ("Crear shaders", E_FAIL);
        return true;
    }

    // --- Dibujo ------------------------------------------------------------------------------

    void draw (ID3D11ShaderResourceView* view, ID3D11PixelShader* shader, juce::Rectangle<float> rect,
               float targetW, float targetH, float opacity)
    {
        Placement p {};
        p.dest[0] = rect.getX() / targetW * 2.0f - 1.0f;
        p.dest[1] = 1.0f - rect.getY() / targetH * 2.0f;
        p.dest[2] = rect.getRight() / targetW * 2.0f - 1.0f;
        p.dest[3] = 1.0f - rect.getBottom() / targetH * 2.0f;
        p.uv[0] = 0.0f; p.uv[1] = 0.0f; p.uv[2] = 1.0f; p.uv[3] = 1.0f;
        p.params[0] = opacity;
        context->UpdateSubresource (constants.Get(), 0, nullptr, &p, 0, 0);
        context->PSSetShader (shader, nullptr, 0);
        context->PSSetShaderResources (0, 1, &view);
        context->Draw (4, 0);
    }

    void setViewport (float w, float h)
    {
        D3D11_VIEWPORT vp {};
        vp.Width = w;
        vp.Height = h;
        vp.MaxDepth = 1.0f;
        context->RSSetViewports (1, &vp);
    }

    void bindPipeline()
    {
        context->IASetInputLayout (nullptr);
        context->IASetPrimitiveTopology (D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context->VSSetShader (vertexShader.Get(), nullptr, 0);
        context->VSSetConstantBuffers (0, 1, constants.GetAddressOf());
        context->PSSetConstantBuffers (0, 1, constants.GetAddressOf());
        context->PSSetSamplers (0, 1, sampler.GetAddressOf());
        context->RSSetState (rasterizer.Get());
    }

    // Sube la imagen de una capa si cambio (o si cambio de tamano, se crea otra textura).
    SlotTexture* upload (const Layer& layer)
    {
        auto& slot = slots[layer.slot];
        slot.used = true;
        const int w = layer.image.getWidth(), h = layer.image.getHeight();
        if (slot.texture == nullptr || slot.width != w || slot.height != h)
        {
            slot = {};
            slot.used = true;
            D3D11_TEXTURE2D_DESC td {};
            td.Width = (UINT) w;
            td.Height = (UINT) h;
            td.MipLevels = td.ArraySize = 1;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;   // tal cual (sRGB premultiplicado); el shader la pasa a lineal
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED (device->CreateTexture2D (&td, nullptr, &slot.texture))
                || FAILED (device->CreateShaderResourceView (slot.texture.Get(), nullptr, &slot.view)))
            {
                slot = {};
                return nullptr;
            }
            slot.width = w;
            slot.height = h;
            slot.version = layer.version + 1;   // fuerza la subida
        }
        if (slot.version != layer.version)
        {
            juce::Image::BitmapData data (layer.image, juce::Image::BitmapData::readOnly);
            if (data.pixelStride != 4)
                return nullptr;   // solo ARGB
            context->UpdateSubresource (slot.texture.Get(), 0, nullptr, data.data, (UINT) data.lineStride, 0);
            slot.version = layer.version;
        }
        return &slot;
    }

    void compose (const std::vector<Layer>& layers)
    {
        const float w = (float) settings.width, h = (float) settings.height;
        const float black[4] = { 0, 0, 0, 1 };
        context->OMSetRenderTargets (1, programTarget.GetAddressOf(), nullptr);
        context->ClearRenderTargetView (programTarget.Get(), black);
        context->OMSetBlendState (premultipliedBlend.Get(), nullptr, 0xffffffff);
        setViewport (w, h);
        for (auto& [id, slot] : slots)
            slot.used = false;
        for (const auto& layer : layers)
        {
            if (! layer.image.isValid() || layer.image.getFormat() != juce::Image::ARGB || layer.opacity <= 0.0f)
                continue;
            if (auto* slot = upload (layer))
            {
                const auto area = layer.area.isEmpty()
                                      ? fitInside ((float) slot->width, (float) slot->height, { w, h })
                                      : layer.area;
                draw (slot->view.Get(), layerShader.Get(), area, w, h, juce::jlimit (0.0f, 1.0f, layer.opacity));
            }
        }
        // Las texturas de capas que ya no estan se sueltan (p. ej. al quitar un video).
        for (auto it = slots.begin(); it != slots.end();)
            it = it->second.used ? std::next (it) : slots.erase (it);
        ID3D11ShaderResourceView* none = nullptr;
        context->PSSetShaderResources (0, 1, &none);
    }

    void present()
    {
        const std::lock_guard<std::mutex> sl (outputsLock);
        context->OMSetBlendState (nullptr, nullptr, 0xffffffff);
        for (auto& out : outputs)
        {
            if (out.failed || ! prepareOutput (out))
                continue;
            const float black[4] = { 0, 0, 0, 1 };
            context->OMSetRenderTargets (1, out.target.GetAddressOf(), nullptr);
            context->ClearRenderTargetView (out.target.Get(), black);
            setViewport ((float) out.width, (float) out.height);
            const auto area = fitInside ((float) settings.width, (float) settings.height,
                                         { (float) out.width, (float) out.height });
            draw (programView.Get(), copyShader.Get(), area, (float) out.width, (float) out.height, 1.0f);
            ID3D11ShaderResourceView* none = nullptr;
            context->PSSetShaderResources (0, 1, &none);
            const HRESULT hr = out.swapChain->Present (0, 0);   // sin esperar al refresco: el ritmo lo lleva el hilo
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
                out.failed = true;
        }
        ID3D11RenderTargetView* none = nullptr;
        context->OMSetRenderTargets (1, &none, nullptr);
    }

    // Crea la cadena de intercambio de la ventana, o la redimensiona si cambio de tamano.
    bool prepareOutput (Output& out)
    {
        RECT rc {};
        if (! IsWindow (out.hwnd) || ! GetClientRect (out.hwnd, &rc))
            return false;
        const int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0)
            return false;   // minimizada
        if (out.swapChain == nullptr)
        {
            DXGI_SWAP_CHAIN_DESC1 sd {};
            sd.Width = (UINT) w;
            sd.Height = (UINT) h;
            sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            sd.SampleDesc.Count = 1;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.BufferCount = 2;
            sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            if (FAILED (factory->CreateSwapChainForHwnd (device.Get(), out.hwnd, &sd, nullptr, nullptr, &out.swapChain)))
            {
                // Windows sin modelo flip: el clasico.
                sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
                sd.BufferCount = 1;
                if (FAILED (factory->CreateSwapChainForHwnd (device.Get(), out.hwnd, &sd, nullptr, nullptr, &out.swapChain)))
                {
                    out.failed = true;
                    return false;
                }
            }
            factory->MakeWindowAssociation (out.hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
            out.width = w;
            out.height = h;
        }
        else if (w != out.width || h != out.height)
        {
            out.target.Reset();
            if (FAILED (out.swapChain->ResizeBuffers (0, (UINT) w, (UINT) h, DXGI_FORMAT_UNKNOWN, 0)))
            {
                out.failed = true;
                return false;
            }
            out.width = w;
            out.height = h;
        }
        if (out.target == nullptr)
        {
            ComPtr<ID3D11Texture2D> back;
            D3D11_RENDER_TARGET_VIEW_DESC rtv {};
            rtv.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;   // vista sRGB de un bufer normal: lo admite el modelo flip
            rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            if (FAILED (out.swapChain->GetBuffer (0, IID_PPV_ARGS (&back)))
                || FAILED (device->CreateRenderTargetView (back.Get(), &rtv, &out.target)))
            {
                out.failed = true;
                return false;
            }
        }
        return true;
    }

    void renderFrame (const std::vector<Layer>& layers)
    {
        const std::lock_guard<std::mutex> sl (gpuLock);
        bindPipeline();
        compose (layers);
        present();
        ++frames;
    }

    // --- Hilo a ritmo fijo (como obs_graphics_thread) ----------------------------------------

    void run()
    {
        // Temporizador de alta resolucion (Windows 10 1803+); si no hay, Sleep con 1 ms de resolucion.
        HANDLE timer = CreateWaitableTimerExW (nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
        if (timer == nullptr)
            timeBeginPeriod (1);
        LARGE_INTEGER freq {}, now {};
        QueryPerformanceFrequency (&freq);
        const double interval = (double) freq.QuadPart / settings.fps;
        QueryPerformanceCounter (&now);
        double next = (double) now.QuadPart;
        std::vector<Layer> layers;

        while (running.load())
        {
            LARGE_INTEGER begin {};
            QueryPerformanceCounter (&begin);
            layers.clear();
            if (build != nullptr && *build)
                (*build) (layers);
            renderFrame (layers);
            QueryPerformanceCounter (&now);
            const double ms = (double) (now.QuadPart - begin.QuadPart) * 1000.0 / (double) freq.QuadPart;
            renderMs = renderMs.load() * 0.95 + ms * 0.05;

            next += interval;
            if ((double) now.QuadPart > next)
            {
                // Llego tarde: los fotogramas perdidos se cuentan y se sigue desde ahora.
                const auto missed = (juce::uint64) (((double) now.QuadPart - next) / interval) + 1;
                lagged += missed;
                next += (double) missed * interval;
            }
            const double waitMs = (next - (double) now.QuadPart) * 1000.0 / (double) freq.QuadPart;
            if (waitMs > 0.0)
            {
                if (timer != nullptr)
                {
                    LARGE_INTEGER due {};
                    due.QuadPart = -(LONGLONG) (waitMs * 10000.0);   // relativo, en 100 ns
                    SetWaitableTimer (timer, &due, 0, nullptr, nullptr, FALSE);
                    WaitForSingleObject (timer, INFINITE);
                }
                else
                {
                    Sleep ((DWORD) waitMs);
                }
            }
        }
        if (timer != nullptr) CloseHandle (timer);
        else                  timeEndPeriod (1);
    }
};

Compositor::Compositor (Settings s) : settings (s), impl (std::make_unique<Impl>())
{
    settings.width = juce::jlimit (64, 3840, settings.width);
    settings.height = juce::jlimit (64, 2160, settings.height);
    settings.fps = juce::jlimit (1, 120, settings.fps);
    impl->settings = settings;
    impl->build = &buildFrame;
    impl->create();
}

Compositor::~Compositor()
{
    stop();
}

bool Compositor::isReady() const { return impl->device != nullptr; }
juce::String Compositor::getError() const { return impl->error; }

void Compositor::start()
{
    if (! isReady() || impl->running.exchange (true))
        return;
    impl->thread = std::thread ([this]
    {
        juce::Thread::setCurrentThreadName ("MiDJ compositor");
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        impl->run();
    });
}

void Compositor::stop()
{
    impl->running = false;
    if (impl->thread.joinable())
        impl->thread.join();
}

bool Compositor::isRunning() const { return impl->running.load(); }

void Compositor::addOutput (void* hwnd)
{
    const std::lock_guard<std::mutex> sl (impl->outputsLock);
    for (const auto& out : impl->outputs)
        if (out.hwnd == (HWND) hwnd)
            return;
    Output out;
    out.hwnd = (HWND) hwnd;
    impl->outputs.push_back (std::move (out));
}

void Compositor::removeOutput (void* hwnd)
{
    // Al volver, la cadena de intercambio esta liberada: ya se puede destruir la ventana.
    // (Mismo orden de cerrojos que el hilo: primero la GPU, luego las salidas.)
    const std::lock_guard<std::mutex> gl (impl->gpuLock);
    const std::lock_guard<std::mutex> sl (impl->outputsLock);
    std::erase_if (impl->outputs, [hwnd] (const Output& o) { return o.hwnd == (HWND) hwnd; });
}

int Compositor::getNumOutputs() const
{
    const std::lock_guard<std::mutex> sl (impl->outputsLock);
    return (int) impl->outputs.size();
}

void Compositor::renderOnce (const std::vector<Layer>& layers)
{
    if (isReady() && ! isRunning())
        impl->renderFrame (layers);
}

juce::Image Compositor::readProgram()
{
    if (! isReady())
        return {};
    const std::lock_guard<std::mutex> sl (impl->gpuLock);
    if (impl->staging == nullptr)
    {
        D3D11_TEXTURE2D_DESC td {};
        impl->program->GetDesc (&td);
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED (impl->device->CreateTexture2D (&td, nullptr, &impl->staging)))
            return {};
    }
    impl->context->CopyResource (impl->staging.Get(), impl->program.Get());
    D3D11_MAPPED_SUBRESOURCE mapped {};
    if (FAILED (impl->context->Map (impl->staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return {};
    juce::Image image (juce::Image::ARGB, settings.width, settings.height, false, juce::SoftwareImageType());
    {
        juce::Image::BitmapData out (image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < settings.height; ++y)
            memcpy (out.getLinePointer (y), (const uint8_t*) mapped.pData + (size_t) y * mapped.RowPitch, (size_t) settings.width * 4);
    }
    impl->context->Unmap (impl->staging.Get(), 0);
    return image;
}

Compositor::Stats Compositor::getStats() const
{
    Stats s;
    s.frames = impl->frames.load();
    s.lagged = impl->lagged.load();
    s.renderMs = impl->renderMs.load();
    s.hardware = impl->hardware;
    s.adapter = impl->adapter;
    return s;
}

juce::Rectangle<float> Compositor::fitInside (float w, float h, juce::Rectangle<float> area)
{
    if (w <= 0.0f || h <= 0.0f || area.isEmpty())
        return {};
    const float scale = juce::jmin (area.getWidth() / w, area.getHeight() / h);
    const float fw = w * scale, fh = h * scale;
    return { area.getX() + (area.getWidth() - fw) * 0.5f, area.getY() + (area.getHeight() - fh) * 0.5f, fw, fh };
}
}
