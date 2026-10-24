#include "Video/Scenes.h"

namespace midj
{
namespace
{
    const char* typeIds[] = { "decks", "deckA", "deckB", "image", "text", "colour", "screen", "window", "visualizer", "media", "captureDevice", "youtube" };
    const char* visualizerIds[] = { "spectrum", "wave", "pulse" };

    const char* autoId (SceneAuto when)
    {
        switch (when)
        {
            case SceneAuto::audio:   return "audio";
            case SceneAuto::video:   return "video";
            case SceneAuto::idle:    return "idle";
            case SceneAuto::youtube: return "youtube";
            case SceneAuto::none:
            default:                 return "";
        }
    }
    const char* transitionIds[] = { "cut", "fade", "fadeToBlack", "slide", "swipe" };
    const char* customKindIds[] = { "stinger", "luma", "colour" };

    template <typename Enum, size_t N>
    Enum parseId (const juce::String& s, const char* (&ids)[N], Enum fallback)
    {
        for (size_t i = 0; i < N; ++i)
            if (s == ids[i])
                return (Enum) i;
        return fallback;
    }
}

// --- Coleccion -------------------------------------------------------------------------------

SceneCollection SceneCollection::createDefault (int w, int h)
{
    SceneCollection c;
    auto& scene = c.addScene ("Mezcla");
    scene.items.push_back (c.makeItem (SourceType::decks, w, h));
    c.currentScene = scene.id;
    return c;
}

int SceneCollection::newId()
{
    return ++lastId;
}

Scene& SceneCollection::addScene (const juce::String& name)
{
    Scene s;
    s.id = newId();
    s.name = name;
    scenes.push_back (std::move (s));
    return scenes.back();
}

Scene* SceneCollection::findScene (int id)
{
    for (auto& s : scenes)
        if (s.id == id)
            return &s;
    return nullptr;
}

const Scene* SceneCollection::findScene (int id) const
{
    return const_cast<SceneCollection*> (this)->findScene (id);
}

SceneItem* SceneCollection::findItem (int itemId)
{
    for (auto& s : scenes)
        for (auto& i : s.items)
            if (i.id == itemId)
                return &i;
    return nullptr;
}

void SceneItem::resetSourceSettings()
{
    const SceneItem d;
    loop = d.loop;
    restartOnActivate = d.restartOnActivate;
    clearOnEnd = d.clearOnEnd;
    closeWhenInactive = d.closeWhenInactive;
    speedPercent = d.speedPercent;
    matte = d.matte;
    hardwareDecode = d.hardwareDecode;
    volume = d.volume;
    muted = d.muted;
    customFormat = d.customFormat;
    captureWidth = d.captureWidth;
    captureHeight = d.captureHeight;
    captureFps = d.captureFps;
    captureFormat = d.captureFormat;
    flipVertical = d.flipVertical;
    deactivateWhenHidden = d.deactivateWhenHidden;
    deviceActive = d.deviceActive;
    cursor = d.cursor;
    clientArea = d.clientArea;
    if (type == SourceType::text)
    {
        bold = d.bold;
        shadow = d.shadow;
    }
}

SceneItem SceneCollection::makeItem (SourceType type, int w, int h)
{
    SceneItem item;
    item.id = newId();
    item.type = type;
    item.name = typeName (type);
    switch (type)
    {
        case SourceType::text:
            // Como un rotulo de "ahora suena" abajo a la izquierda.
            item.text = "{artista} - {titulo}";
            item.fontSize = (float) h * 0.05f;
            item.position = { (float) w * 0.04f, (float) h * 0.82f };
            break;
        case SourceType::colour:
            // Una franja oscura semitransparente abajo (para poner un texto encima).
            item.colour = juce::Colours::black.withAlpha (0.6f);
            item.width = w;
            item.height = h / 5;
            item.position = { 0.0f, (float) (h - item.height) };
            break;
        case SourceType::visualizer:
            // Un espectro en el tercio de abajo, a lo ancho.
            item.colour = juce::Colour (0xff36c8ff);
            item.width = w;
            item.height = h / 3;
            item.position = { 0.0f, (float) (h - item.height) };
            break;
        case SourceType::decks:
        case SourceType::deckA:
        case SourceType::deckB:
        case SourceType::image:
        case SourceType::screen:
        case SourceType::window:
        default:
            break;   // a pantalla completa (la imagen se coloca al elegir el archivo)
    }
    return item;
}

juce::String SceneCollection::autoName (SceneAuto when)
{
    switch (when)
    {
        case SceneAuto::audio:   return "Cuando suena una cancion de audio";
        case SceneAuto::video:   return "Cuando suena un video";
        case SceneAuto::idle:    return "Cuando no suena nada";
        case SceneAuto::youtube: return "Cuando suena YouTube";
        case SceneAuto::none:
        default:                 return "Nunca";
    }
}

void SceneCollection::setAuto (int sceneId, int when)
{
    for (auto& s : scenes)
        if (s.id == sceneId)
            s.autoWhen = when;
        else
            s.autoWhen &= ~when;   // cada cambio automatico, en una sola escena
}

juce::String SceneCollection::typeName (SourceType type)
{
    switch (type)
    {
        case SourceType::decks:  return "Mezcla de decks";
        case SourceType::deckA:  return "Deck A";
        case SourceType::deckB:  return "Deck B";
        case SourceType::image:  return "Imagen";
        case SourceType::text:   return "Texto";
        case SourceType::colour: return "Color";
        case SourceType::screen: return "Captura de pantalla";
        case SourceType::window: return "Captura de ventana";
        case SourceType::visualizer: return "Visualizador";
        case SourceType::media: return "Multimedia";
        case SourceType::captureDevice: return "Dispositivo de captura de video";
        case SourceType::youtube: return "YouTube";
    }
    return {};
}

juce::String SceneCollection::visualizerName (VisualizerStyle style)
{
    switch (style)
    {
        case VisualizerStyle::spectrum: return "Espectro";
        case VisualizerStyle::wave:     return "Onda";
        case VisualizerStyle::pulse:    return "Pulso";
    }
    return {};
}

juce::String SceneCollection::transitionName (TransitionType type)
{
    switch (type)
    {
        case TransitionType::cut:         return "Corte";
        case TransitionType::fade:        return "Fundido";
        case TransitionType::fadeToBlack: return "Fundido a negro";
        case TransitionType::slide:       return "Deslizar";
        case TransitionType::swipe:       return "Barrido";
    }
    return {};
}

std::unique_ptr<juce::XmlElement> SceneCollection::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("SCENES");
    xml->setAttribute ("current", currentScene);
    xml->setAttribute ("transition", transitionIds[(int) transition.type]);
    xml->setAttribute ("ms", transition.milliseconds);
    if (transition.custom != 0)
        xml->setAttribute ("customTransition", transition.custom);
    for (const auto& t : customTransitions)
    {
        auto* e = xml->createNewChildElement ("CUSTOM_TRANSITION");
        e->setAttribute ("id", t.id);
        e->setAttribute ("name", t.name);
        e->setAttribute ("kind", customKindIds[(int) t.kind]);
        e->setAttribute ("file", t.file.getFullPathName());
        e->setAttribute ("ms", t.milliseconds);
        e->setAttribute ("cut", t.cutMilliseconds);
        e->setAttribute ("softness", t.softness);
        e->setAttribute ("colour", t.colour.toString());
        if (t.matte)
            e->setAttribute ("matte", true);
    }
    for (const auto& s : scenes)
    {
        auto* se = xml->createNewChildElement ("SCENE");
        se->setAttribute ("id", s.id);
        se->setAttribute ("name", s.name);
        if (s.autoWhen != 0)
        {
            juce::StringArray when;
            for (auto a : allSceneAutos)
                if (s.switchesOn (a))
                    when.add (autoId (a));
            se->setAttribute ("auto", when.joinIntoString (","));
        }
        for (const auto& i : s.items)
        {
            auto* e = se->createNewChildElement ("ITEM");
            e->setAttribute ("id", i.id);
            e->setAttribute ("type", typeIds[(int) i.type]);
            e->setAttribute ("name", i.name);
            e->setAttribute ("visible", i.visible);
            e->setAttribute ("x", i.position.x);
            e->setAttribute ("y", i.position.y);
            e->setAttribute ("sx", i.scaleX);
            e->setAttribute ("sy", i.scaleY);
            e->setAttribute ("rotation", i.rotation);
            e->setAttribute ("opacity", i.opacity);
            e->setAttribute ("cropLeft", i.cropLeft);
            e->setAttribute ("cropTop", i.cropTop);
            e->setAttribute ("cropRight", i.cropRight);
            e->setAttribute ("cropBottom", i.cropBottom);
            if (i.type == SourceType::image)
                e->setAttribute ("file", i.file.getFullPathName());
            if (i.type == SourceType::text)
            {
                e->setAttribute ("text", i.text);
                e->setAttribute ("size", i.fontSize);
                e->setAttribute ("bold", i.bold);
                e->setAttribute ("shadow", i.shadow);
            }
            if (i.type == SourceType::text || i.type == SourceType::colour || i.type == SourceType::visualizer)
                e->setAttribute ("colour", i.colour.toString());
            if (i.type == SourceType::visualizer)
                e->setAttribute ("style", visualizerIds[(int) i.visualizer]);
            if (i.type == SourceType::colour || i.type == SourceType::visualizer)
            {
                e->setAttribute ("width", i.width);
                e->setAttribute ("height", i.height);
            }
            if (i.type == SourceType::screen)
            {
                e->setAttribute ("monitor", i.monitor);
                e->setAttribute ("monitorId", i.monitorId);
            }
            if (i.type == SourceType::window)
            {
                e->setAttribute ("windowTitle", i.windowTitle);
                e->setAttribute ("windowClass", i.windowClass);
                e->setAttribute ("windowExe", i.windowExe);
                e->setAttribute ("clientArea", i.clientArea);
            }
            if (i.type == SourceType::screen || i.type == SourceType::window)
                e->setAttribute ("cursor", i.cursor);
            if (i.locked)
                e->setAttribute ("locked", true);
            if (i.type == SourceType::media)
            {
                e->setAttribute ("file", i.file.getFullPathName());
                e->setAttribute ("loop", i.loop);
                e->setAttribute ("restart", i.restartOnActivate);
                e->setAttribute ("clearOnEnd", i.clearOnEnd);
                e->setAttribute ("closeInactive", i.closeWhenInactive);
                e->setAttribute ("speed", i.speedPercent);
                if (i.matte)
                    e->setAttribute ("matte", true);
            }
            if (i.type == SourceType::captureDevice)
            {
                e->setAttribute ("deviceId", i.deviceId);
                e->setAttribute ("deviceName", i.deviceName);
                e->setAttribute ("customFormat", i.customFormat);
                e->setAttribute ("captureWidth", i.captureWidth);
                e->setAttribute ("captureHeight", i.captureHeight);
                e->setAttribute ("captureFps", i.captureFps);
                e->setAttribute ("captureFormat", i.captureFormat);
                e->setAttribute ("flip", i.flipVertical);
                e->setAttribute ("deactivateHidden", i.deactivateWhenHidden);
                e->setAttribute ("active", i.deviceActive);
                e->setAttribute ("audioDevice", i.audioDevice);
            }
            if (i.type == SourceType::media || i.type == SourceType::captureDevice)
            {
                e->setAttribute ("hwDecode", i.hardwareDecode);
                e->setAttribute ("volume", i.volume);
                e->setAttribute ("muted", i.muted);
            }
        }
    }
    return xml;
}

SceneCollection SceneCollection::fromXml (const juce::XmlElement& xml, int w, int h)
{
    SceneCollection c;
    for (auto* se : xml.getChildWithTagNameIterator ("SCENE"))
    {
        Scene s;
        s.id = se->getIntAttribute ("id");
        s.name = se->getStringAttribute ("name", "Escena");
        for (const auto& when : juce::StringArray::fromTokens (se->getStringAttribute ("auto"), ",", {}))
            for (auto a : allSceneAutos)
                if (when.trim() == autoId (a))
                    s.autoWhen |= (int) a;
        c.lastId = juce::jmax (c.lastId, s.id);
        for (auto* e : se->getChildWithTagNameIterator ("ITEM"))
        {
            SceneItem i;
            i.id = e->getIntAttribute ("id");
            c.lastId = juce::jmax (c.lastId, i.id);
            i.type = parseId (e->getStringAttribute ("type"), typeIds, SourceType::decks);
            i.name = e->getStringAttribute ("name", typeName (i.type));
            i.visible = e->getBoolAttribute ("visible", true);
            i.position = { (float) e->getDoubleAttribute ("x"), (float) e->getDoubleAttribute ("y") };
            i.scaleX = (float) e->getDoubleAttribute ("sx", 1.0);
            i.scaleY = (float) e->getDoubleAttribute ("sy", 1.0);
            i.rotation = (float) e->getDoubleAttribute ("rotation");
            i.opacity = (float) e->getDoubleAttribute ("opacity", 1.0);
            i.cropLeft = (float) e->getDoubleAttribute ("cropLeft");
            i.cropTop = (float) e->getDoubleAttribute ("cropTop");
            i.cropRight = (float) e->getDoubleAttribute ("cropRight");
            i.cropBottom = (float) e->getDoubleAttribute ("cropBottom");
            i.file = juce::File (e->getStringAttribute ("file"));
            i.text = e->getStringAttribute ("text");
            i.fontSize = (float) e->getDoubleAttribute ("size", i.fontSize);
            i.bold = e->getBoolAttribute ("bold", true);
            i.shadow = e->getBoolAttribute ("shadow", true);
            if (e->hasAttribute ("colour"))
                i.colour = juce::Colour::fromString (e->getStringAttribute ("colour"));
            i.width = e->getIntAttribute ("width", w);
            i.height = e->getIntAttribute ("height", h);
            i.monitor = e->getIntAttribute ("monitor");
            i.monitorId = e->getStringAttribute ("monitorId");
            i.windowTitle = e->getStringAttribute ("windowTitle");
            i.windowClass = e->getStringAttribute ("windowClass");
            i.windowExe = e->getStringAttribute ("windowExe");
            i.clientArea = e->getBoolAttribute ("clientArea", true);
            i.cursor = e->getBoolAttribute ("cursor", true);
            i.visualizer = parseId (e->getStringAttribute ("style"), visualizerIds, VisualizerStyle::spectrum);
            i.locked = e->getBoolAttribute ("locked");
            i.loop = e->getBoolAttribute ("loop", i.loop);
            i.restartOnActivate = e->getBoolAttribute ("restart", i.restartOnActivate);
            i.clearOnEnd = e->getBoolAttribute ("clearOnEnd", i.clearOnEnd);
            i.closeWhenInactive = e->getBoolAttribute ("closeInactive", i.closeWhenInactive);
            i.speedPercent = juce::jlimit (1, 200, e->getIntAttribute ("speed", i.speedPercent));
            i.matte = e->getBoolAttribute ("matte");
            i.deviceId = e->getStringAttribute ("deviceId");
            i.deviceName = e->getStringAttribute ("deviceName");
            i.customFormat = e->getBoolAttribute ("customFormat");
            i.captureWidth = e->getIntAttribute ("captureWidth");
            i.captureHeight = e->getIntAttribute ("captureHeight");
            i.captureFps = e->getDoubleAttribute ("captureFps");
            i.captureFormat = e->getStringAttribute ("captureFormat");
            i.flipVertical = e->getBoolAttribute ("flip");
            i.deactivateWhenHidden = e->getBoolAttribute ("deactivateHidden");
            i.deviceActive = e->getBoolAttribute ("active", true);
            i.audioDevice = e->getStringAttribute ("audioDevice");
            i.hardwareDecode = e->getBoolAttribute ("hwDecode", i.hardwareDecode);
            i.volume = (float) e->getDoubleAttribute ("volume", 1.0);
            i.muted = e->getBoolAttribute ("muted");
            s.items.push_back (std::move (i));
        }
        c.scenes.push_back (std::move (s));
    }
    if (c.scenes.empty())
        return createDefault (w, h);
    c.currentScene = xml.getIntAttribute ("current", c.scenes.front().id);
    if (c.findScene (c.currentScene) == nullptr)
        c.currentScene = c.scenes.front().id;
    c.transition.type = parseId (xml.getStringAttribute ("transition"), transitionIds, TransitionType::fade);
    c.transition.milliseconds = juce::jlimit (0, 10000, xml.getIntAttribute ("ms", 600));
    for (auto* e : xml.getChildWithTagNameIterator ("CUSTOM_TRANSITION"))
    {
        CustomTransition t;
        t.id = e->getIntAttribute ("id");
        if (t.id <= 0)
            continue;
        t.name = e->getStringAttribute ("name", "Personalizada");
        t.kind = parseId (e->getStringAttribute ("kind"), customKindIds, CustomTransitionKind::stinger);
        if (const auto path = e->getStringAttribute ("file"); juce::File::isAbsolutePath (path))
            t.file = juce::File (path);
        t.milliseconds = juce::jlimit (100, 20000, e->getIntAttribute ("ms", 1200));
        t.cutMilliseconds = juce::jlimit (0, t.milliseconds, e->getIntAttribute ("cut", t.milliseconds / 2));
        t.softness = juce::jlimit (0.0f, 1.0f, (float) e->getDoubleAttribute ("softness", 0.2));
        t.colour = juce::Colour::fromString (e->getStringAttribute ("colour", "ff000000"));
        t.matte = e->getBoolAttribute ("matte");
        c.lastId = juce::jmax (c.lastId, t.id);
        c.customTransitions.push_back (t);
    }
    c.transition.custom = xml.getIntAttribute ("customTransition");
    if (c.findCustom (c.transition.custom) == nullptr)
        c.transition.custom = 0;
    return c;
}

const CustomTransition* SceneCollection::findCustom (int id) const
{
    for (const auto& t : customTransitions)
        if (t.id == id)
            return &t;
    return nullptr;
}

juce::String SceneCollection::customKindName (CustomTransitionKind kind)
{
    switch (kind)
    {
        case CustomTransitionKind::stinger: return "Stinger";
        case CustomTransitionKind::luma:    return "Barrido con imagen";
        case CustomTransitionKind::colour:  return "A color";
    }
    return {};
}

// --- Geometria -------------------------------------------------------------------------------

namespace SceneGeometry
{
    static juce::Rectangle<float> cropped (const SceneItem& item, juce::Point<int> natural)
    {
        const float w = juce::jmax (1.0f, (float) natural.x - item.cropLeft - item.cropRight);
        const float h = juce::jmax (1.0f, (float) natural.y - item.cropTop - item.cropBottom);
        return { item.cropLeft, item.cropTop, w, h };
    }

    juce::Rectangle<float> box (const SceneItem& item, juce::Point<int> natural)
    {
        const auto c = cropped (item, natural);
        return { item.position.x, item.position.y, c.getWidth() * item.scaleX, c.getHeight() * item.scaleY };
    }

    Compositor::Quad quad (const SceneItem& item, juce::Point<int> natural)
    {
        return Compositor::corners (box (item, natural), item.rotation);
    }

    bool contains (const SceneItem& item, juce::Point<int> natural, juce::Point<float> p)
    {
        const auto b = box (item, natural);
        // Al reves: el punto se gira hacia atras alrededor del centro y se mira en el recuadro sin girar.
        p.applyTransform (juce::AffineTransform::rotation (-juce::degreesToRadians (item.rotation), b.getCentreX(), b.getCentreY()));
        return b.contains (p);
    }

    bool place (Compositor::Layer& layer, const SceneItem& item, juce::Point<int> natural, float pw, float ph)
    {
        const juce::Rectangle<float> canvas { 0.0f, 0.0f, (float) natural.x, (float) natural.y };
        const auto picture = Compositor::fitInside (pw, ph, canvas);   // donde queda la imagen en el lienzo
        const auto visible = cropped (item, natural).getIntersection (picture);
        if (visible.isEmpty() || picture.isEmpty())
            return false;
        const auto b = box (item, natural);
        const auto c = cropped (item, natural);
        layer.uv = { (visible.getX() - picture.getX()) / picture.getWidth(), (visible.getY() - picture.getY()) / picture.getHeight(),
                     visible.getWidth() / picture.getWidth(), visible.getHeight() / picture.getHeight() };
        layer.area = { b.getX() + (visible.getX() - c.getX()) * item.scaleX, b.getY() + (visible.getY() - c.getY()) * item.scaleY,
                       visible.getWidth() * item.scaleX, visible.getHeight() * item.scaleY };
        layer.rotation = 0.0f;
        layer.transform = juce::AffineTransform::rotation (juce::degreesToRadians (item.rotation), b.getCentreX(), b.getCentreY());
        layer.opacity *= juce::jlimit (0.0f, 1.0f, item.opacity);
        return true;
    }

    juce::String resolveText (const juce::String& text, const NowPlaying& now)
    {
        auto s = text.replace ("{titulo}", now.title, true)
                     .replace ("{artista}", now.artist, true)
                     .replace ("{bpm}", now.bpm > 0.0 ? juce::String (now.bpm, 1) : juce::String(), true)
                     .replace ("{yt_titulo}", now.youtubeTitle, true)
                     .replace ("{yt_canal}", now.youtubeChannel, true);
        // Sin artista (o sin cancion), no quedan guiones sueltos: "{artista} - {titulo}" -> "Titulo".
        juce::StringArray lines;
        for (auto line : juce::StringArray::fromLines (s))
        {
            line = line.trim();
            while (line.startsWith ("-")) line = line.substring (1).trimStart();
            while (line.endsWith ("-"))   line = line.dropLastCharacters (1).trimEnd();
            lines.add (line);
        }
        return lines.joinIntoString ("\n");
    }

    float textDensity (const SceneItem& item)
    {
        const float scale = juce::jmax (std::abs (item.scaleX), std::abs (item.scaleY));
        const float stepped = std::round (scale * 4.0f) / 4.0f;
        const float maxForFont = 1000.0f / juce::jlimit (6.0f, 400.0f, item.fontSize);
        return juce::jlimit (0.25f, juce::jmax (0.25f, juce::jmin (4.0f, maxForFont)), stepped);
    }

    juce::Image renderText (const SceneItem& item, const juce::String& text, float density)
    {
        density = juce::jlimit (0.1f, 8.0f, density);
        const juce::Font font (juce::FontOptions (juce::jlimit (6.0f, 400.0f, item.fontSize) * density, item.bold ? juce::Font::bold : juce::Font::plain));
        const auto lines = juce::StringArray::fromLines (text);
        float width = 0.0f;
        for (const auto& line : lines)
            width = juce::jmax (width, juce::GlyphArrangement::getStringWidth (font, line));
        const float lineHeight = font.getHeight();
        const float pad = (item.shadow ? juce::jmax (2.0f, item.fontSize * 0.06f) : 1.0f) * density;
        const int w = juce::jmax (1, (int) std::ceil (width + pad * 2.0f));
        const int h = juce::jmax (1, (int) std::ceil (lineHeight * (float) juce::jmax (1, lines.size()) + pad * 2.0f));
        juce::Image image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
        if (text.trim().isEmpty())
            return image;
        juce::Graphics g (image);
        g.setFont (font);
        auto drawLines = [&] (juce::Point<float> offset, juce::Colour colour)
        {
            g.setColour (colour);
            for (int i = 0; i < lines.size(); ++i)
                g.drawSingleLineText (lines[i], juce::roundToInt (pad + offset.x),
                                      juce::roundToInt (pad + offset.y + lineHeight * (float) i + font.getAscent()));
        };
        if (item.shadow)
            drawLines ({ pad * 0.6f, pad * 0.6f }, juce::Colours::black.withAlpha (0.65f));
        drawLines ({}, item.colour);
        return image;
    }
}
}
