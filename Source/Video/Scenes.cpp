#include "Video/Scenes.h"

namespace midj
{
namespace
{
    const char* typeIds[] = { "decks", "deckA", "deckB", "image", "text", "colour", "screen", "window", "visualizer" };
    const char* visualizerIds[] = { "spectrum", "wave", "pulse" };
    const char* transitionIds[] = { "cut", "fade", "fadeToBlack", "slide", "swipe" };

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
    for (const auto& s : scenes)
    {
        auto* se = xml->createNewChildElement ("SCENE");
        se->setAttribute ("id", s.id);
        se->setAttribute ("name", s.name);
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
    return c;
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
                     .replace ("{bpm}", now.bpm > 0.0 ? juce::String (now.bpm, 1) : juce::String(), true);
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

    juce::Image renderText (const SceneItem& item, const juce::String& text)
    {
        const juce::Font font (juce::FontOptions (juce::jlimit (6.0f, 400.0f, item.fontSize), item.bold ? juce::Font::bold : juce::Font::plain));
        const auto lines = juce::StringArray::fromLines (text);
        float width = 0.0f;
        for (const auto& line : lines)
            width = juce::jmax (width, juce::GlyphArrangement::getStringWidth (font, line));
        const float lineHeight = font.getHeight();
        const float pad = item.shadow ? juce::jmax (2.0f, item.fontSize * 0.06f) : 1.0f;
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
