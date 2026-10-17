#include "Video/GpuFrames.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace midj
{
struct GpuFrameSource::Texture
{
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    UINT width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

void GpuFrameSource::publish (std::shared_ptr<GpuVideoFrame> frame)
{
    if (frame == nullptr)
        return;
    width = frame->width;
    height = frame->height;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (pending, frame);
    }
    showing = true;
    ++frameNumber;   // el anterior (si el compositor no llego a copiarlo) se suelta aqui, fuera del cerrojo
}

void GpuFrameSource::clear()
{
    showing = false;
    std::shared_ptr<GpuVideoFrame> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (pending, old);
    }
    ++frameNumber;
}

Compositor::GpuSource::Picture GpuFrameSource::update (void* d, void* c)
{
    std::shared_ptr<GpuVideoFrame> frame;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (pending, frame);
    }
    if (! showing.load())
        return {};
    auto* device = static_cast<ID3D11Device*> (d);
    auto* context = static_cast<ID3D11DeviceContext*> (c);
    if (frame != nullptr)
    {
        auto* source = static_cast<ID3D11Texture2D*> (frame->getTexture());
        D3D11_TEXTURE2D_DESC sd {};
        source->GetDesc (&sd);
        const UINT w = (UINT) frame->width, h = (UINT) frame->height;
        // Una textura propia del mismo formato (la de Media Foundation no siempre se puede leer
        // desde un shader y se reutiliza); se crea otra solo si cambia el tamano o el formato.
        if (texture == nullptr || texture->width != w || texture->height != h || texture->format != sd.Format)
        {
            auto t = std::make_shared<Texture>();
            D3D11_TEXTURE2D_DESC td {};
            td.Width = w;
            td.Height = h;
            td.MipLevels = td.ArraySize = 1;
            td.Format = sd.Format;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED (device->CreateTexture2D (&td, nullptr, &t->texture))
                || FAILED (device->CreateShaderResourceView (t->texture.Get(), nullptr, &t->view)))
                return {};
            t->width = w;
            t->height = h;
            t->format = sd.Format;
            texture = std::move (t);
        }
        // Solo la parte visible (el decodificador puede alinear la textura a 16 pixeles).
        const D3D11_BOX box { 0, 0, 0, juce::jmin (w, sd.Width), juce::jmin (h, sd.Height), 1 };
        context->CopySubresourceRegion (texture->texture.Get(), 0, 0, 0, 0, source, (UINT) frame->getSubresource(), &box);
        frame.reset();   // Media Foundation ya puede reutilizar su textura
    }
    if (texture == nullptr)
        return {};
    Picture p;
    p.view = texture->view.Get();
    p.width = (int) texture->width;
    p.height = (int) texture->height;
    p.opaque = true;   // el video no tiene transparencia
    return p;
}
}
