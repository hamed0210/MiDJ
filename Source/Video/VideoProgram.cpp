#include "Video/VideoProgram.h"

namespace midj
{
namespace
{
    constexpr int itemSlotBase = 1000;   // ranuras del compositor de imagenes y textos: 1000 + id del elemento

    // La fuente YouTube es una captura de la ventana del reproductor de MiDJ (sin cursor).
    SceneItem youTubeCapture (const SceneItem& item)
    {
        auto w = item;
        w.type = SourceType::window;
        w.windowTitle = YouTubeWindow::title;
        w.windowClass = YouTubeWindow::className;
        w.windowExe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFileName();
        w.cursor = false;
        w.clientArea = false;
        return w;
    }
}

VideoProgram::VideoProgram (VideoMix& m, int w, int h) : mix (m), width (w), height (h)
{
}

juce::Point<int> VideoProgram::naturalSize (const SceneItem& item) const
{
    switch (item.type)
    {
        case SourceType::image:
        case SourceType::text:
            if (auto it = cache.find (item.id); it != cache.end() && it->second.image.isValid())
                return { it->second.image.getWidth(), it->second.image.getHeight() };
            return { width / 4, height / 4 };
        case SourceType::colour:
        case SourceType::visualizer:
            return { juce::jmax (1, item.width), juce::jmax (1, item.height) };
        case SourceType::media:
        case SourceType::captureDevice:
            if (auto s = media != nullptr ? media->find (item.id) : nullptr)
                if (const auto size = s->getSize(); size.x > 0 && size.y > 0)
                    return size;
            return { width, height };
        case SourceType::screen:
        case SourceType::window:
            if (auto c = captures != nullptr ? captures->find (item.id) : nullptr)
                if (const auto s = c->getSize(); s.x > 0 && s.y > 0)
                    return s;
            return { width, height };
        case SourceType::decks:
        case SourceType::deckA:
        case SourceType::deckB:
        default:
            return { width, height };
    }
}

VideoProgram::Prepared VideoProgram::prepare (const SceneItem& item)
{
    Prepared p;
    p.item = item;
    juce::String key;
    switch (item.type)
    {
        case SourceType::image:
            key = "img|" + item.file.getFullPathName() + "|" + juce::String (item.file.getLastModificationTime().toMilliseconds());
            break;
        case SourceType::text:
            key = "txt|" + SceneGeometry::resolveText (item.text, now) + "|" + juce::String (item.fontSize) + "|"
                  + juce::String ((int) item.bold) + juce::String ((int) item.shadow) + "|" + item.colour.toString();
            break;
        case SourceType::colour:
            key = "col|" + item.colour.toString();
            break;
        case SourceType::screen:
        case SourceType::window:
        case SourceType::youtube:
            if (captures != nullptr)
                p.capture = captures->get (item.type == SourceType::youtube ? youTubeCapture (item) : item);
            p.natural = naturalSize (item);
            return p;   // la imagen la pone la captura en la GPU en cada fotograma
        case SourceType::media:
        case SourceType::captureDevice:
            if (media != nullptr)
                p.frames = media->get (item);
            p.natural = naturalSize (item);
            return p;   // la imagen la deja su hilo; el compositor la sube cuando cambia
        case SourceType::visualizer:
            p.natural = naturalSize (item);
            return p;   // se dibuja en el hilo del compositor en cada fotograma
        case SourceType::decks:
        case SourceType::deckA:
        case SourceType::deckB:
        default:
            p.natural = naturalSize (item);
            return p;   // la imagen la da la mezcla en cada fotograma
    }

    auto& cached = cache[item.id];
    if (cached.key != key)
    {
        cached.key = key;
        cached.version = nextVersion++;
        if (item.type == SourceType::image)
        {
            auto loaded = juce::ImageFileFormat::loadFrom (item.file);
            cached.image = loaded.isValid() ? loaded.convertedToFormat (juce::Image::ARGB) : juce::Image();
        }
        else if (item.type == SourceType::text)
        {
            cached.image = SceneGeometry::renderText (item, SceneGeometry::resolveText (item.text, now));
        }
        else
        {
            cached.image = juce::Image (juce::Image::ARGB, 1, 1, true, juce::SoftwareImageType());
            cached.image.setPixelAt (0, 0, item.colour);
        }
    }
    p.image = cached.image;
    p.version = cached.version;
    p.natural = naturalSize (item);
    return p;
}

void VideoProgram::republish()
{
    auto scenes = std::make_shared<std::vector<PreparedScene>>();
    std::set<int> alive;
    for (const auto& s : collection.scenes)
    {
        PreparedScene ps;
        ps.id = s.id;
        for (const auto& item : s.items)
        {
            alive.insert (item.id);
            ps.items.push_back (prepare (item));
        }
        scenes->push_back (std::move (ps));
    }
    // Lo preparado para elementos que ya no existen se suelta.
    if (captures != nullptr)
        captures->keepOnly (alive);
    if (media != nullptr)
        media->keepOnly (alive);
    for (auto it = cache.begin(); it != cache.end();)
        it = alive.count (it->first) > 0 ? std::next (it) : cache.erase (it);

    const std::lock_guard<std::mutex> sl (lock);
    published = std::move (scenes);
    if (current == 0 || collection.findScene (current) == nullptr)
    {
        current = collection.currentScene;
        transitioning = false;
    }
}

void VideoProgram::setScenes (const SceneCollection& scenes)
{
    collection = scenes;
    republish();
}

void VideoProgram::setNowPlaying (const NowPlaying& n)
{
    if (n == now)
        return;
    now = n;
    republish();
}

void VideoProgram::switchTo (int sceneId)
{
    // La imagen del barrido se lee aqui (hilo de interfaz), no con el cerrojo.
    const auto* chosen = collection.currentCustom();
    const auto image = chosen != nullptr && chosen->kind == CustomTransitionKind::luma ? maskFor (*chosen) : juce::Image();
    const std::lock_guard<std::mutex> sl (lock);
    if (sceneId == current && ! transitioning)
        return;
    if (image.isValid() && ! image.isNull() && (! mask.isValid() || mask != image))
    {
        mask = image;
        ++maskVersion;
    }
    beginTransition (current, sceneId, chosen);
}

void VideoProgram::previewTransition (const CustomTransition& t)
{
    const auto image = t.kind == CustomTransitionKind::luma ? maskFor (t) : juce::Image();
    const std::lock_guard<std::mutex> sl (lock);
    if (image.isValid() && mask != image)
    {
        mask = image;
        ++maskVersion;
    }
    beginTransition (current, current, &t);
}

void VideoProgram::setStinger (std::shared_ptr<FrameSource> source)
{
    const std::lock_guard<std::mutex> sl (lock);
    stinger = std::move (source);
}

void VideoProgram::beginTransition (int from, int to, const CustomTransition* chosen)
{
    transition = collection.transition;
    custom.reset();
    if (chosen != nullptr)
        custom = *chosen;
    const int ms = custom ? custom->milliseconds : transition.milliseconds;
    if ((! custom && transition.type == TransitionType::cut) || ms <= 0 || from == 0)
    {
        current = to;
        transitioning = false;
        return;
    }
    previous = from;
    current = to;
    transitionStart = juce::Time::getMillisecondCounterHiRes();
    transitioning = true;
}

juce::Image VideoProgram::maskFor (const CustomTransition& t)
{
    const auto key = t.file.getFullPathName() + "|" + juce::String (t.file.getLastModificationTime().toMilliseconds());
    if (auto it = maskCache.find (key); it != maskCache.end())
        return it->second;
    juce::Image image;
    if (t.file.existsAsFile())
        image = juce::ImageFileFormat::loadFrom (t.file);
    if (image.isValid())
    {
        // Pequena basta (el borde se suaviza en la GPU) y en ARGB, que es lo que sube el compositor.
        const int w = juce::jmin (image.getWidth(), 640), h = juce::jmax (1, image.getHeight() * w / juce::jmax (1, image.getWidth()));
        image = image.rescaled (w, h, juce::Graphics::mediumResamplingQuality).convertedToFormat (juce::Image::ARGB);
    }
    else
    {
        // Sin imagen: un barrido en diagonal (de arriba a la izquierda a abajo a la derecha).
        image = juce::Image (juce::Image::ARGB, 256, 144, false);
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const auto v = (juce::uint8) juce::jlimit (0, 255, (x * 255 / 256 + y * 255 / 144) / 2);
                image.setPixelAt (x, y, juce::Colour (v, v, v));
            }
    }
    maskCache[key] = image;
    return image;
}

void VideoProgram::customLayers (const CustomTransition& c, float t, const PreparedScene& outgoing, const PreparedScene& incoming,
                                 const juce::Image& maskImage, juce::uint64 maskImageVersion,
                                 const std::shared_ptr<FrameSource>& stingerSource, Compositor::Frame& frame)
{
    constexpr int colourSlot = 900, maskSlot = 901, stingerSlot = 902;
    const juce::Rectangle<float> full { 0.0f, 0.0f, (float) width, (float) height };
    switch (c.kind)
    {
        case CustomTransitionKind::colour:
        {
            // Primera mitad: la escena vieja se tapa con el color; segunda: el color se va y queda la nueva.
            const bool first = t < 0.5f;
            sceneLayers (first ? outgoing : incoming, frame.layers);
            Compositor::Layer colour;
            colour.slot = colourSlot;
            colour.image = juce::Image (juce::Image::ARGB, 2, 2, false);
            colour.image.clear (colour.image.getBounds(), c.colour.withAlpha (1.0f));
            colour.version = ((juce::uint64) 1 << 40) | c.colour.getARGB();
            colour.area = full;
            colour.opacity = first ? 2.0f * t : 2.0f - 2.0f * t;
            frame.layers.push_back (colour);
            break;
        }
        case CustomTransitionKind::luma:
        {
            // La nueva entra por encima de la vieja por donde la imagen es mas oscura.
            sceneLayers (outgoing, frame.scenes[0]);
            sceneLayers (incoming, frame.scenes[1]);
            frame.useScene = { true, true };
            Compositor::Layer out, in;
            out.source = 0;
            in.source = 1;
            out.area = in.area = full;
            in.mask = maskImage;
            in.maskVersion = maskImageVersion;
            in.maskSlot = maskSlot;
            in.maskProgress = t;
            in.maskSoftness = juce::jlimit (0.005f, 0.5f, c.softness * 0.5f);
            frame.layers.push_back (out);
            frame.layers.push_back (in);
            break;
        }
        case CustomTransitionKind::stinger:
        {
            // Debajo, la escena vieja hasta el punto de cambio y luego la nueva; encima, el video.
            const float cut = (float) c.cutMilliseconds / (float) juce::jmax (1, c.milliseconds);
            sceneLayers (t < cut ? outgoing : incoming, frame.layers);
            if (stingerSource != nullptr)
            {
                Compositor::Layer layer;
                layer.slot = stingerSlot;
                if (stingerSource->getGpu()->hasPicture())
                    layer.gpu = stingerSource->getGpu();
                else
                {
                    layer.image = stingerSource->getFrame();
                    layer.version = ((juce::uint64) 1 << 60) + stingerSource->getFrameNumber();
                }
                if (VideoMix::hasPicture (layer))
                    frame.layers.push_back (layer);
            }
            break;
        }
    }
}

std::set<int> VideoProgram::liveItems() const
{
    int sceneId = 0;
    {
        const std::lock_guard<std::mutex> sl (lock);
        sceneId = current;
    }
    std::set<int> ids;
    if (const auto* scene = collection.findScene (sceneId))
        for (const auto& item : scene->items)
            if (item.visible)
                ids.insert (item.id);
    return ids;
}

bool VideoProgram::isInTransition() const
{
    const std::lock_guard<std::mutex> sl (lock);
    return transitioning;
}

float VideoProgram::easeInOut (float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow (-2.0f * t + 2.0f, 3.0f) * 0.5f;   // cubica, como el deslizar de OBS
}

void VideoProgram::transitionLayers (TransitionType type, float t, float w, float h, std::vector<Compositor::Layer>& layers)
{
    Compositor::Layer outgoing, incoming;
    outgoing.source = 0;
    incoming.source = 1;
    outgoing.area = incoming.area = { 0.0f, 0.0f, w, h };
    t = juce::jlimit (0.0f, 1.0f, t);
    switch (type)
    {
        case TransitionType::fade:   // en luz lineal: a la mitad no oscurece
            incoming.opacity = t;
            layers.push_back (outgoing);
            layers.push_back (incoming);
            break;
        case TransitionType::fadeToBlack:   // la primera mitad baja a negro, la segunda sube la nueva
            if (t < 0.5f)
            {
                outgoing.opacity = 1.0f - 2.0f * t;
                layers.push_back (outgoing);
            }
            else
            {
                incoming.opacity = 2.0f * t - 1.0f;
                layers.push_back (incoming);
            }
            break;
        case TransitionType::slide:   // las dos se mueven hacia la izquierda
        {
            const float e = easeInOut (t);
            outgoing.area.setX (-e * w);
            incoming.area.setX ((1.0f - e) * w);
            layers.push_back (outgoing);
            layers.push_back (incoming);
            break;
        }
        case TransitionType::swipe:   // la nueva entra por encima de la vieja
            incoming.area.setX ((1.0f - easeInOut (t)) * w);
            layers.push_back (outgoing);
            layers.push_back (incoming);
            break;
        case TransitionType::cut:
        default:
            layers.push_back (incoming);
            break;
    }
}

void VideoProgram::sceneLayers (const PreparedScene& scene, std::vector<Compositor::Layer>& layers)
{
    for (const auto& p : scene.items)
    {
        const auto& item = p.item;
        if (! item.visible || item.opacity <= 0.0f)
            continue;
        auto add = [&] (Compositor::Layer layer, float pw, float ph)
        {
            if (VideoMix::hasPicture (layer) && SceneGeometry::place (layer, item, p.natural, pw, ph))
                layers.push_back (std::move (layer));
        };
        switch (item.type)
        {
            case SourceType::decks:
            {
                auto a = mix.picture (0), b = mix.picture (1);
                const bool hasA = VideoMix::hasPicture (a), hasB = VideoMix::hasPicture (b);
                if (! hasA && ! hasB)
                {
                    add (mix.logoLayer(), (float) width, (float) height);
                    break;
                }
                // Fundido cruzado como en la ventana de siempre: A debajo, B encima con su parte.
                const auto w = VideoMix::weights (mix.getCrossfader(), hasA, hasB);
                if (w[0] > 0.0f)
                    add (a, (float) VideoMix::pictureSize (a).x, (float) VideoMix::pictureSize (a).y);
                if (w[1] > 0.0f)
                {
                    b.opacity *= w[0] > 0.0f ? w[1] / (w[0] + w[1]) : 1.0f;
                    add (b, (float) VideoMix::pictureSize (b).x, (float) VideoMix::pictureSize (b).y);
                }
                break;
            }
            case SourceType::deckA:
            case SourceType::deckB:
            {
                auto pic = mix.picture (item.type == SourceType::deckA ? 0 : 1);
                if (VideoMix::hasPicture (pic))
                    add (pic, (float) VideoMix::pictureSize (pic).x, (float) VideoMix::pictureSize (pic).y);
                break;
            }
            case SourceType::screen:
            case SourceType::window:
            case SourceType::youtube:
            {
                if (p.capture == nullptr)
                    break;
                const auto size = p.capture->getSize();
                if (size.x <= 0 || size.y <= 0)
                    break;   // aun sin imagen
                Compositor::Layer layer;
                layer.gpu = p.capture;
                // El tamano natural es el de la ultima imagen (la ventana puede cambiar de tamano).
                if (SceneGeometry::place (layer, item, size, (float) size.x, (float) size.y))
                    layers.push_back (std::move (layer));
                break;
            }
            case SourceType::media:
            case SourceType::captureDevice:
            {
                if (p.frames == nullptr)
                    break;
                Compositor::Layer layer;
                if (p.frames->getGpu()->hasPicture())
                {
                    layer.gpu = p.frames->getGpu();   // el fotograma sigue en la tarjeta grafica
                }
                else
                {
                    layer.slot = itemSlotBase + item.id;
                    layer.image = p.frames->getFrame();
                    layer.version = (1ull << 61) + p.frames->getFrameNumber();   // otra serie que las imagenes y el visualizador
                }
                if (VideoMix::hasPicture (layer))
                    add (layer, (float) VideoMix::pictureSize (layer).x, (float) VideoMix::pictureSize (layer).y);
                break;
            }
            case SourceType::visualizer:
            {
                if (tap == nullptr)
                    break;
                auto& vis = visualizers[item.id];
                if (vis == nullptr)
                    vis = std::make_unique<Visualizer> (*tap);
                visualizersSeen.insert (item.id);
                Compositor::Layer layer;
                layer.slot = itemSlotBase + item.id;
                layer.image = vis->render (item, juce::Time::getMillisecondCounterHiRes());
                layer.version = (1ull << 62) + ++visualizerFrame;   // cambia en cada fotograma (otra serie que la de las imagenes)
                add (layer, (float) p.natural.x, (float) p.natural.y);
                break;
            }
            case SourceType::image:
            case SourceType::text:
            case SourceType::colour:
            default:
            {
                Compositor::Layer layer;
                layer.slot = itemSlotBase + item.id;
                layer.image = p.image;
                layer.version = p.version;
                add (layer, (float) p.natural.x, (float) p.natural.y);   // llena su lienzo (el color se estira)
                break;
            }
        }
    }
}

void VideoProgram::soloLayers (const std::vector<PreparedScene>& scenes, int itemId, std::vector<Compositor::Layer>& layers)
{
    for (const auto& s : scenes)
        for (const auto& p : s.items)
        {
            if (p.item.id != itemId)
                continue;
            // La fuente entera: visible, sin recorte, sin giro, a su tamano natural...
            PreparedScene alone;
            auto& copy = alone.items.emplace_back (p);
            copy.item.visible = true;
            copy.item.opacity = 1.0f;
            copy.item.rotation = 0.0f;
            copy.item.position = {};
            copy.item.scaleX = copy.item.scaleY = 1.0f;
            copy.item.cropLeft = copy.item.cropTop = copy.item.cropRight = copy.item.cropBottom = 0.0f;
            const auto first = layers.size();
            sceneLayers (alone, layers);
            if (layers.size() == first)
                return;
            // ...y luego encajada sin deformar en todo el programa.
            auto bounds = layers[first].area;
            for (auto k = first; k < layers.size(); ++k)
                bounds = bounds.getUnion (layers[k].area);
            const auto fitted = Compositor::fitInside (bounds.getWidth(), bounds.getHeight(), { 0.0f, 0.0f, (float) width, (float) height });
            if (fitted.isEmpty())
                return;
            const float scale = fitted.getWidth() / bounds.getWidth();
            for (auto k = first; k < layers.size(); ++k)
            {
                auto& a = layers[k].area;
                a = { fitted.getX() + (a.getX() - bounds.getX()) * scale, fitted.getY() + (a.getY() - bounds.getY()) * scale,
                      a.getWidth() * scale, a.getHeight() * scale };
                layers[k].transform = {};
            }
            return;
        }
}

void VideoProgram::buildFrame (Compositor::Frame& frame)
{
    std::shared_ptr<const std::vector<PreparedScene>> scenes;
    int cur = 0, prev = 0;
    bool inTransition = false;
    float t = 1.0f;
    TransitionType type = TransitionType::cut;
    std::optional<CustomTransition> own;
    juce::Image ownMask;
    juce::uint64 ownMaskVersion = 0;
    std::shared_ptr<FrameSource> ownStinger;
    {
        const std::lock_guard<std::mutex> sl (lock);
        scenes = published;
        if (transitioning)
        {
            // Como OBS: el progreso es el tiempo pasado sobre la duracion.
            const int ms = custom ? custom->milliseconds : transition.milliseconds;
            t = (float) ((juce::Time::getMillisecondCounterHiRes() - transitionStart) / juce::jmax (1, ms));
            if (t >= 1.0f)
                transitioning = false;
        }
        cur = current;
        prev = previous;
        inTransition = transitioning;
        type = transition.type;
        if (inTransition && custom)
        {
            own = custom;
            ownMask = mask;
            ownMaskVersion = maskVersion;
            ownStinger = stinger;
        }
    }
    if (scenes == nullptr)
        return;
    visualizersSeen.clear();
    if (const int solo = soloItem.load(); solo != 0)
        soloLayers (*scenes, solo, frame.solo);
    auto find = [&] (int id) -> const PreparedScene*
    {
        for (const auto& s : *scenes)
            if (s.id == id)
                return &s;
        return nullptr;
    };
    const auto* incoming = find (cur);
    const auto* outgoing = inTransition ? find (prev) : nullptr;
    if (incoming == nullptr)
        return;
    if (outgoing == nullptr)
    {
        sceneLayers (*incoming, frame.layers);
    }
    else if (own)
    {
        customLayers (*own, juce::jlimit (0.0f, 1.0f, t), *outgoing, *incoming, ownMask, ownMaskVersion, ownStinger, frame);
    }
    else
    {
        sceneLayers (*outgoing, frame.scenes[0]);
        sceneLayers (*incoming, frame.scenes[1]);
        frame.useScene = { true, true };
        transitionLayers (type, t, (float) width, (float) height, frame.layers);
    }
    // Los visualizadores que no salen en este fotograma se sueltan (al volver, empiezan de cero).
    for (auto it = visualizers.begin(); it != visualizers.end();)
        it = visualizersSeen.count (it->first) > 0 ? std::next (it) : visualizers.erase (it);
}
}
