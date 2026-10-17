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
    // Un solo vertex shader: un cuadrilatero (4 vertices sin bufer) con sus esquinas en las
    // constantes, asi sirve para capas giradas. Dos pixel shaders:
    //  - capa: las imagenes de JUCE son premultiplicadas en espacio sRGB; se pasan a luz lineal
    //    (como OBS, que mezcla en lineal: un fundido a la mitad no oscurece) y se aplica la opacidad.
    //    El programa es una textura sRGB: la GPU mezcla en lineal y guarda en sRGB.
    //  - copia: el programa a una salida (las dos vistas sRGB: el filtrado es en lineal).
    const char* shaderSource = R"(
cbuffer Placement : register(b0)
{
    float4 p01;      // esquinas arriba izquierda y arriba derecha en coordenadas de pantalla (-1..1)
    float4 p23;      // abajo izquierda y abajo derecha
    float4 uv;       // zona de la textura (izquierda, arriba, derecha, abajo)
    float4 params;   // x = opacidad, y = 1 si la imagen no tiene alfa (capturas del escritorio)
};
Texture2D image : register(t0);
SamplerState linearClamp : register(s0);

struct VsOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VsOut vs (uint id : SV_VertexID)
{
    float2 corner = float2 (id & 1, id >> 1);   // (0,0) (1,0) (0,1) (1,1): tira de 2 triangulos
    float2 p = id == 0 ? p01.xy : id == 1 ? p01.zw : id == 2 ? p23.xy : p23.zw;
    VsOut o;
    o.pos = float4 (p, 0, 1);
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
    if (params.y > 0.5) c.a = 1;
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
        float points[8];
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
        std::vector<Compositor::Quad> overlay;
    };

    // Una textura que se puede usar de destino (sRGB: mezcla en lineal) y de imagen.
    struct RenderTexture
    {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<ID3D11ShaderResourceView> srgbView;    // se lee en lineal (para copiar a las salidas)
        ComPtr<ID3D11ShaderResourceView> plainView;   // se lee tal cual (como las imagenes, para el shader de capas)
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

    RenderTexture program;
    std::array<RenderTexture, 2> sceneTextures;   // escenas de una transicion
    ComPtr<ID3D11Texture2D> staging;
    ComPtr<ID3D11ShaderResourceView> overlayColour;   // 1x1 del color de la seleccion

    std::map<int, SlotTexture> slots;
    std::map<GpuSource*, GpuSource::Picture> pictures;   // las fuentes de la GPU ya puestas al dia en este fotograma
    std::mutex gpuLock;   // el contexto de Direct3D se usa desde un hilo cada vez

    std::mutex outputsLock;
    std::vector<Output> outputs;

    std::thread thread;
    std::atomic<bool> running { false };
    std::function<void (Frame&)>* build = nullptr;

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

        if (! createRenderTexture (program) || ! createRenderTexture (sceneTextures[0]) || ! createRenderTexture (sceneTextures[1]))
            return false;

        // Color de la seleccion de la vista previa (rojo, como OBS).
        const uint32_t red = 0xffff3030u;
        D3D11_TEXTURE2D_DESC td {};
        td.Width = td.Height = 1;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init { &red, 4, 4 };
        ComPtr<ID3D11Texture2D> redTexture;
        if (FAILED (hr = device->CreateTexture2D (&td, &init, &redTexture))
            || FAILED (hr = device->CreateShaderResourceView (redTexture.Get(), nullptr, &overlayColour)))
            return fail ("Textura de la seleccion", hr);
        return true;
    }

    // Sin tipo para verla como sRGB (mezcla en lineal), tal cual (para el shader de capas) y copiarla a la CPU.
    bool createRenderTexture (RenderTexture& rt)
    {
        D3D11_TEXTURE2D_DESC td {};
        td.Width = (UINT) settings.width;
        td.Height = (UINT) settings.height;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = device->CreateTexture2D (&td, nullptr, &rt.texture);
        if (FAILED (hr))
            return fail ("Textura de composicion", hr);
        D3D11_RENDER_TARGET_VIEW_DESC rtv {};
        rtv.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv {};
        srv.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        D3D11_SHADER_RESOURCE_VIEW_DESC plain = srv;
        plain.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        if (FAILED (hr = device->CreateRenderTargetView (rt.texture.Get(), &rtv, &rt.target))
            || FAILED (hr = device->CreateShaderResourceView (rt.texture.Get(), &srv, &rt.srgbView))
            || FAILED (hr = device->CreateShaderResourceView (rt.texture.Get(), &plain, &rt.plainView)))
            return fail ("Vistas de composicion", hr);
        const float black[4] = { 0, 0, 0, 1 };
        context->ClearRenderTargetView (rt.target.Get(), black);
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

    // Un cuadrilatero (esquinas en pixeles del destino, en el orden de Quad) con una zona de la textura.
    void draw (ID3D11ShaderResourceView* view, ID3D11PixelShader* shader, const Quad& q,
               float targetW, float targetH, juce::Rectangle<float> uv, float opacity, bool opaque = false)
    {
        Placement p {};
        auto put = [&] (int i, juce::Point<float> pt)
        {
            p.points[i * 2] = pt.x / targetW * 2.0f - 1.0f;
            p.points[i * 2 + 1] = 1.0f - pt.y / targetH * 2.0f;
        };
        put (0, q[0]);   // arriba izquierda
        put (1, q[1]);   // arriba derecha
        put (2, q[3]);   // abajo izquierda (orden de la tira de triangulos)
        put (3, q[2]);   // abajo derecha
        p.uv[0] = uv.getX(); p.uv[1] = uv.getY(); p.uv[2] = uv.getRight(); p.uv[3] = uv.getBottom();
        p.params[0] = opacity;
        p.params[1] = opaque ? 1.0f : 0.0f;
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

    // La imagen de una fuente de la GPU, puesta al dia una sola vez por fotograma.
    GpuSource::Picture picture (GpuSource& source)
    {
        if (auto it = pictures.find (&source); it != pictures.end())
            return it->second;
        auto pic = source.update (device.Get(), context.Get());
        if (pic.width <= 0 || pic.height <= 0)
            pic.view = pic.cursorView = nullptr;
        bindPipeline();   // por si la fuente uso el contexto
        return pictures[&source] = pic;
    }

    // El puntero del raton de una captura: su rectangulo en la imagen se lleva al area de la capa
    // (recortado como la imagen) y se gira igual.
    void drawCursor (const Layer& layer, juce::Rectangle<float> area, const GpuSource::Picture& pic, float sw, float sh, float w, float h)
    {
        const juce::Rectangle<float> cursor { pic.cursor.getX() / sw, pic.cursor.getY() / sh, pic.cursor.getWidth() / sw, pic.cursor.getHeight() / sh };
        const auto visible = cursor.getIntersection (layer.uv);
        if (visible.isEmpty() || cursor.isEmpty())
            return;
        auto toArea = [&] (float u, float v)
        {
            return juce::Point<float> { area.getX() + (u - layer.uv.getX()) / layer.uv.getWidth() * area.getWidth(),
                                        area.getY() + (v - layer.uv.getY()) / layer.uv.getHeight() * area.getHeight() };
        };
        const auto sub = juce::Rectangle<float> (toArea (visible.getX(), visible.getY()), toArea (visible.getRight(), visible.getBottom()));
        Quad quad { sub.getTopLeft(), sub.getTopRight(), sub.getBottomRight(), sub.getBottomLeft() };
        const auto spin = juce::AffineTransform::rotation (juce::degreesToRadians (layer.rotation), area.getCentreX(), area.getCentreY())
                              .followedBy (layer.transform);
        for (auto& p : quad)
            p.applyTransform (spin);
        const juce::Rectangle<float> uv { (visible.getX() - cursor.getX()) / cursor.getWidth(), (visible.getY() - cursor.getY()) / cursor.getHeight(),
                                          visible.getWidth() / cursor.getWidth(), visible.getHeight() / cursor.getHeight() };
        draw ((ID3D11ShaderResourceView*) pic.cursorView, layerShader.Get(), quad, w, h, uv, juce::jlimit (0.0f, 1.0f, layer.opacity));
    }

    // Dibuja las capas en una textura de composicion (el programa o una escena).
    void compose (RenderTexture& into, const std::vector<Layer>& layers)
    {
        const float w = (float) settings.width, h = (float) settings.height;
        const float black[4] = { 0, 0, 0, 1 };
        context->OMSetRenderTargets (1, into.target.GetAddressOf(), nullptr);
        context->ClearRenderTargetView (into.target.Get(), black);
        context->OMSetBlendState (premultipliedBlend.Get(), nullptr, 0xffffffff);
        setViewport (w, h);
        for (const auto& layer : layers)
        {
            if (layer.opacity <= 0.0f || layer.uv.isEmpty())
                continue;
            ID3D11ShaderResourceView* view = nullptr;
            float sw = w, sh = h;
            GpuSource::Picture gpu;
            if (layer.gpu != nullptr)
            {
                gpu = picture (*layer.gpu);
                view = (ID3D11ShaderResourceView*) gpu.view;
                sw = (float) gpu.width;
                sh = (float) gpu.height;
            }
            else if (layer.source == imageSource)
            {
                if (! layer.image.isValid() || layer.image.getFormat() != juce::Image::ARGB)
                    continue;
                auto* slot = upload (layer);
                if (slot == nullptr)
                    continue;
                view = slot->view.Get();
                sw = (float) slot->width;
                sh = (float) slot->height;
            }
            else if (juce::isPositiveAndBelow (layer.source, 2) && &into != &sceneTextures[(size_t) layer.source])
            {
                view = sceneTextures[(size_t) layer.source].plainView.Get();
            }
            if (view == nullptr)
                continue;
            const auto area = layer.area.isEmpty()
                                  ? fitInside (sw * layer.uv.getWidth(), sh * layer.uv.getHeight(), { w, h })
                                  : layer.area;
            auto quad = corners (area, layer.rotation);
            if (! layer.transform.isIdentity())
                for (auto& p : quad)
                    p.applyTransform (layer.transform);
            draw (view, layerShader.Get(), quad, w, h, layer.uv, juce::jlimit (0.0f, 1.0f, layer.opacity), gpu.opaque && gpu.view != nullptr);
            if (gpu.cursorView != nullptr)
                drawCursor (layer, area, gpu, sw, sh, w, h);
        }
        ID3D11ShaderResourceView* none = nullptr;
        context->PSSetShaderResources (0, 1, &none);
        ID3D11RenderTargetView* noTarget = nullptr;
        context->OMSetRenderTargets (1, &noTarget, nullptr);
    }

    void present()
    {
        const std::lock_guard<std::mutex> sl (outputsLock);
        for (auto& out : outputs)
        {
            if (out.failed || ! prepareOutput (out))
                continue;
            const float black[4] = { 0, 0, 0, 1 };
            context->OMSetRenderTargets (1, out.target.GetAddressOf(), nullptr);
            context->OMSetBlendState (nullptr, nullptr, 0xffffffff);
            context->ClearRenderTargetView (out.target.Get(), black);
            const float ow = (float) out.width, oh = (float) out.height;
            setViewport (ow, oh);
            const auto area = fitInside ((float) settings.width, (float) settings.height, { ow, oh });
            draw (program.srgbView.Get(), copyShader.Get(), corners (area, 0.0f), ow, oh, { 0.0f, 0.0f, 1.0f, 1.0f }, 1.0f);
            if (! out.overlay.empty())
                drawOverlay (out, area);
            ID3D11ShaderResourceView* none = nullptr;
            context->PSSetShaderResources (0, 1, &none);
            const HRESULT hr = out.swapChain->Present (0, 0);   // sin esperar al refresco: el ritmo lo lleva el hilo
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
                out.failed = true;
        }
        ID3D11RenderTargetView* none = nullptr;
        context->OMSetRenderTargets (1, &none, nullptr);
    }

    // Los recuadros de seleccion: lados de 2 pixeles de pantalla y asas de 8 en las esquinas.
    void drawOverlay (const Output& out, juce::Rectangle<float> area)
    {
        context->OMSetBlendState (premultipliedBlend.Get(), nullptr, 0xffffffff);
        const float sx = area.getWidth() / (float) settings.width, sy = area.getHeight() / (float) settings.height;
        const float ow = (float) out.width, oh = (float) out.height;
        const juce::Rectangle<float> all { 0.0f, 0.0f, 1.0f, 1.0f };
        for (const auto& box : out.overlay)
        {
            Quad q;
            for (size_t i = 0; i < 4; ++i)
                q[i] = { area.getX() + box[i].x * sx, area.getY() + box[i].y * sy };
            for (size_t i = 0; i < 4; ++i)
            {
                const auto a = q[i], b = q[(i + 1) % 4];
                const auto d = b - a;
                const float len = std::sqrt (d.x * d.x + d.y * d.y);
                if (len < 0.5f)
                    continue;
                const juce::Point<float> n { -d.y / len, d.x / len };   // normal de 1 pixel
                draw (overlayColour.Get(), layerShader.Get(), { a - n, b - n, b + n, a + n }, ow, oh, all, 1.0f);
            }
            for (const auto& c : q)
                draw (overlayColour.Get(), layerShader.Get(), corners ({ c.x - 4.0f, c.y - 4.0f, 8.0f, 8.0f }, 0.0f), ow, oh, all, 1.0f);
        }
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

    void renderFrame (const Frame& frame)
    {
        const std::lock_guard<std::mutex> sl (gpuLock);
        bindPipeline();
        for (auto& [id, slot] : slots)
            slot.used = false;
        pictures.clear();
        // Primero se ponen al dia las capturas (copian en sus texturas), luego se compone.
        for (const auto* list : { &frame.scenes[0], &frame.scenes[1], &frame.layers })
            for (const auto& layer : *list)
                if (layer.gpu != nullptr)
                    picture (*layer.gpu);
        for (size_t s = 0; s < 2; ++s)
            if (frame.useScene[s])
                compose (sceneTextures[s], frame.scenes[s]);
        compose (program, frame.layers);
        // Las texturas de capas que ya no estan se sueltan (p. ej. al quitar un video).
        for (auto it = slots.begin(); it != slots.end();)
            it = it->second.used ? std::next (it) : slots.erase (it);
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
        Frame frame;

        while (running.load())
        {
            LARGE_INTEGER begin {};
            QueryPerformanceCounter (&begin);
            frame.clear();
            if (build != nullptr && *build)
                (*build) (frame);
            renderFrame (frame);
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
void* Compositor::getDevice() const { return impl->device.Get(); }
juce::String Compositor::getError() const { return impl->error; }

void Compositor::start()
{
    if (! isReady() || impl->running.exchange (true))
        return;
    impl->thread = std::thread ([this]
    {
        juce::Thread::setCurrentThreadName ("MiDJ compositor");
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        // COM multihilo: las capturas de Windows (WinRT) se leen desde este hilo.
        const HRESULT com = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        impl->run();
        if (SUCCEEDED (com))
            CoUninitialize();
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

void Compositor::setOverlay (void* hwnd, std::vector<Quad> boxes)
{
    const std::lock_guard<std::mutex> sl (impl->outputsLock);
    for (auto& out : impl->outputs)
        if (out.hwnd == (HWND) hwnd)
            out.overlay = std::move (boxes);
}

void Compositor::renderOnce (const std::vector<Layer>& layers)
{
    Frame frame;
    frame.layers = layers;
    renderOnce (frame);
}

void Compositor::renderOnce (const Frame& frame)
{
    if (isReady() && ! isRunning())
        impl->renderFrame (frame);
}

juce::Image Compositor::readProgram()
{
    if (! isReady())
        return {};
    const std::lock_guard<std::mutex> sl (impl->gpuLock);
    if (impl->staging == nullptr)
    {
        D3D11_TEXTURE2D_DESC td {};
        impl->program.texture->GetDesc (&td);
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED (impl->device->CreateTexture2D (&td, nullptr, &impl->staging)))
            return {};
    }
    impl->context->CopyResource (impl->staging.Get(), impl->program.texture.Get());
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

Compositor::Quad Compositor::corners (juce::Rectangle<float> area, float rotationDegrees)
{
    Quad q { area.getTopLeft(), area.getTopRight(), area.getBottomRight(), area.getBottomLeft() };
    if (rotationDegrees != 0.0f)
    {
        const auto t = juce::AffineTransform::rotation (juce::degreesToRadians (rotationDegrees), area.getCentreX(), area.getCentreY());
        for (auto& p : q)
            p.applyTransform (t);
    }
    return q;
}
}
