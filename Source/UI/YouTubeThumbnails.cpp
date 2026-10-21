#include "UI/YouTubeThumbnails.h"

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
                image = image.rescaled (160, 90, juce::Graphics::mediumResamplingQuality);
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
        g.drawImage (image, area, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
}
}
