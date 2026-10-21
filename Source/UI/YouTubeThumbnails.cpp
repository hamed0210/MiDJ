#include "UI/YouTubeThumbnails.h"
#include "UI/Icons.h"

namespace midj
{
YouTubeThumbnails::YouTubeThumbnails() = default;

YouTubeThumbnails::~YouTubeThumbnails()
{
    *alive = false;
    pool.removeAllJobs (true, 3000);
}

juce::Image YouTubeThumbnails::get (const juce::String& videoId)
{
    if (videoId.isEmpty())
        return {};
    if (auto it = images.find (videoId); it != images.end())
        return it->second;
    if (requested.insert (videoId).second)
    {
        std::weak_ptr<bool> flag = alive;
        pool.addJob ([this, flag, videoId]
        {
            // mqdefault: 320x180 sin bandas negras (las otras traen el video en 4:3 con franjas).
            const juce::URL url ("https://i.ytimg.com/vi/" + videoId + "/mqdefault.jpg");
            juce::Image image;
            // Primero a memoria: el lector de JPEG vuelve atras en el flujo y el de la red no puede.
            juce::MemoryBlock data;
            if (auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                         .withConnectionTimeoutMs (10000)))
                stream->readIntoMemoryBlock (data);
            if (data.getSize() > 0)
                image = juce::ImageFileFormat::loadFrom (data.getData(), data.getSize());
            if (image.isValid())
            {
                // En la memoria normal (no Direct2D): en este hilo, la primera reescalada en la
                // tarjeta grafica salia negra.
                const auto source = juce::SoftwareImageType().convert (image);
                juce::Image small (juce::Image::RGB, 160, 90, true, juce::SoftwareImageType());
                {
                    juce::Graphics g (small);
                    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
                    g.drawImage (source, 0, 0, 160, 90, 0, 0, source.getWidth(), source.getHeight());
                }
                image = small;
            }
            juce::MessageManager::callAsync ([this, flag, videoId, image]
            {
                if (flag.expired() || ! *flag.lock() || ! image.isValid())
                    return;
                images[videoId] = image;
                sendChangeMessage();
            });
        });
    }
    return {};
}

void YouTubeThumbnails::draw (juce::Graphics& g, const juce::Image& image, juce::Rectangle<float> area, float corner)
{
    juce::Graphics::ScopedSaveState s (g);
    juce::Path clip;
    clip.addRoundedRectangle (area, corner);
    g.reduceClipRegion (clip);
    g.setColour (juce::Colours::black);
    g.fillRect (area);
    if (image.isValid())
    {
        g.drawImage (image, area, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
        return;
    }
    // Sin miniatura (aun no llega o no se pudo bajar): el logo de YouTube.
    g.setColour (juce::Colour (0xff1d1d22));
    g.fillRect (area);
    const float size = juce::jmin (area.getHeight() * 0.55f, area.getWidth() * 0.45f);
    Icons::drawSource (g, area.withSizeKeepingCentre (size, size), SourceType::youtube, juce::Colour (0xffff4e45));
}
}
