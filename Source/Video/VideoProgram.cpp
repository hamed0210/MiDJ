#include "Video/VideoProgram.h"

namespace midj
{
namespace
{
    constexpr int itemSlotBase = 1000;   // ranuras del compositor de imagenes y textos: 1000 + id del elemento
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
            return { juce::jmax (1, item.width), juce::jmax (1, item.height) };
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
            if (captures != nullptr)
                p.capture = captures->get (item);
            p.natural = naturalSize (item);
            return p;   // la imagen la pone la captura en la GPU en cada fotograma
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
    const std::lock_guard<std::mutex> sl (lock);
    if (sceneId == current && ! transitioning)
        return;
    transition = collection.transition;
    if (transition.type == TransitionType::cut || transition.milliseconds <= 0 || current == 0)
    {
        current = sceneId;
        transitioning = false;
        return;
    }
    previous = current;
    current = sceneId;
    transitionStart = juce::Time::getMillisecondCounterHiRes();
    transitioning = true;
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
            if (layer.image.isValid() && SceneGeometry::place (layer, item, p.natural, pw, ph))
                layers.push_back (std::move (layer));
        };
        switch (item.type)
        {
            case SourceType::decks:
            {
                auto a = mix.picture (0), b = mix.picture (1);
                const bool hasA = a.image.isValid(), hasB = b.image.isValid();
                if (! hasA && ! hasB)
                {
                    add (mix.logoLayer(), (float) width, (float) height);
                    break;
                }
                // Fundido cruzado como en la ventana de siempre: A debajo, B encima con su parte.
                const auto w = VideoMix::weights (mix.getCrossfader(), hasA, hasB);
                if (w[0] > 0.0f)
                    add (a, (float) a.image.getWidth(), (float) a.image.getHeight());
                if (w[1] > 0.0f)
                {
                    b.opacity = w[0] > 0.0f ? w[1] / (w[0] + w[1]) : 1.0f;
                    add (b, (float) b.image.getWidth(), (float) b.image.getHeight());
                }
                break;
            }
            case SourceType::deckA:
            case SourceType::deckB:
            {
                auto pic = mix.picture (item.type == SourceType::deckA ? 0 : 1);
                if (pic.image.isValid())
                    add (pic, (float) pic.image.getWidth(), (float) pic.image.getHeight());
                break;
            }
            case SourceType::screen:
            case SourceType::window:
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

void VideoProgram::buildFrame (Compositor::Frame& frame)
{
    std::shared_ptr<const std::vector<PreparedScene>> scenes;
    int cur = 0, prev = 0;
    bool inTransition = false;
    float t = 1.0f;
    TransitionType type = TransitionType::cut;
    {
        const std::lock_guard<std::mutex> sl (lock);
        scenes = published;
        if (transitioning)
        {
            // Como OBS: el progreso es el tiempo pasado sobre la duracion.
            t = (float) ((juce::Time::getMillisecondCounterHiRes() - transitionStart) / juce::jmax (1, transition.milliseconds));
            if (t >= 1.0f)
                transitioning = false;
        }
        cur = current;
        prev = previous;
        inTransition = transitioning;
        type = transition.type;
    }
    if (scenes == nullptr)
        return;
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
        return;
    }
    sceneLayers (*outgoing, frame.scenes[0]);
    sceneLayers (*incoming, frame.scenes[1]);
    frame.useScene = { true, true };
    transitionLayers (type, t, (float) width, (float) height, frame.layers);
}
}
