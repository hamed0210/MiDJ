#include "UI/TransitionDialog.h"
#include "UI/AlphaConvertDialog.h"
#include "UI/DarkLookAndFeel.h"
#include "Video/MediaFoundation.h"
#include "Video/MediaSources.h"

namespace midj
{
// Tarjeta para elegir el tipo: nombre y una linea que dice lo que hace.
class TransitionDialog::KindCard : public juce::Button
{
public:
    KindCard (const juce::String& n, const juce::String& d) : juce::Button (n), description (d) {}

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool on = getToggleState();
        g.setColour (on ? juce::Colour (0xff221d3a) : DarkLookAndFeel::background);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (on ? DarkLookAndFeel::custom : highlighted ? DarkLookAndFeel::textFaint : DarkLookAndFeel::line);
        g.drawRoundedRectangle (r, 10.0f, 1.0f);
        auto area = getLocalBounds().reduced (10, 8);
        g.setColour (DarkLookAndFeel::text);
        g.setFont (DarkLookAndFeel::sans (13.0f, true));
        g.drawText (getName(), area.removeFromTop (18), juce::Justification::centredLeft, false);
        g.setColour (DarkLookAndFeel::textQuiet);
        g.setFont (DarkLookAndFeel::sans (11.0f));
        g.drawFittedText (description, area, juce::Justification::topLeft, 3);
    }

private:
    juce::String description;
};

namespace
{
    void setUpSlider (juce::Slider& s, double min, double max, double step, double value, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 74, 24);
        s.setRange (min, max, step);
        s.setValue (value, juce::dontSendNotification);
        s.setTextValueSuffix (suffix);
        s.setColour (juce::Slider::textBoxOutlineColourId, DarkLookAndFeel::line);
        s.setColour (juce::Slider::textBoxBackgroundColourId, DarkLookAndFeel::background);
        s.setColour (juce::Slider::trackColourId, DarkLookAndFeel::custom);
    }

    const std::array<juce::Colour, 6> swatchColours { juce::Colours::black, juce::Colours::white, DarkLookAndFeel::accentA,
                                                      DarkLookAndFeel::accentB, DarkLookAndFeel::custom, juce::Colour (0xffff4d4d) };
}

TransitionDialog::TransitionDialog()
{
    nameEditor.setTextToShowWhenEmpty ("p. ej. Logo girando", DarkLookAndFeel::textFaint);
    nameEditor.setFont (DarkLookAndFeel::sans (14.0f));
    nameEditor.setIndents (10, 7);
    nameEditor.setSelectAllWhenFocused (true);   // al modificarla, escribir reemplaza el nombre
    nameEditor.onTextChange = [this] { update(); };
    addAndMakeVisible (nameEditor);

    kinds[0] = std::make_unique<KindCard> ("Stinger", "Un video tapa el cambio de escena");
    kinds[1] = std::make_unique<KindCard> ("Barrido", "Una imagen en grises marca por donde entra");
    kinds[2] = std::make_unique<KindCard> ("A color", "Funde a un color y vuelve");
    for (size_t i = 0; i < kinds.size(); ++i)
    {
        kinds[i]->onClick = [this, i] { setKind ((CustomTransitionKind) i); };
        addAndMakeVisible (*kinds[i]);
    }

    fileEditor.setReadOnly (true);
    fileEditor.setFont (DarkLookAndFeel::sans (13.0f));
    fileEditor.setIndents (10, 7);
    addAndMakeVisible (fileEditor);
    browseButton.onClick = [this] { chooseFile(); };
    addAndMakeVisible (browseButton);
    matteToggle.setTooltip ("Para videos con transparencia: la escena se ve por donde el video es transparente "
                            "(se marca solo al elegir un video de doble ancho)");
    matteToggle.onClick = [this] { repaint(); };
    addAndMakeVisible (matteToggle);

    setUpSlider (cutSlider, 0.0, 20000.0, 50.0, 600.0, " ms");
    setUpSlider (durationSlider, 100.0, 20000.0, 50.0, 1200.0, " ms");
    setUpSlider (softnessSlider, 0.0, 100.0, 1.0, 20.0, " %");
    cutSlider.setTooltip ("Cuando cambia la escena debajo del video (normalmente cuando el video tapa toda la pantalla)");
    durationSlider.onValueChange = [this] { cutSlider.setValue (juce::jmin (cutSlider.getValue(), durationSlider.getValue()), juce::dontSendNotification); };
    for (auto* s : { &cutSlider, &durationSlider, &softnessSlider })
        addAndMakeVisible (s);

    for (size_t i = 0; i < swatches.size(); ++i)
    {
        swatches[i].getProperties().set (DarkLookAndFeel::filledProperty, true);
        swatches[i].setColour (juce::TextButton::buttonColourId, swatchColours[i]);
        swatches[i].onClick = [this, i] { setColour (swatchColours[i]); };
        addAndMakeVisible (swatches[i]);
    }
    otherColourButton.onClick = [this]
    {
        auto selector = std::make_unique<juce::ColourSelector> (juce::ColourSelector::showColourspace | juce::ColourSelector::showSliders);
        selector->setCurrentColour (colour);
        selector->setSize (300, 280);
        selector->setColour (juce::ColourSelector::backgroundColourId, juce::Colour (0xff18181b));
        selector->addChangeListener (this);
        juce::CallOutBox::launchAsynchronously (std::move (selector), otherColourButton.getScreenBounds(), nullptr);
    };
    addAndMakeVisible (otherColourButton);

    tryButton.setTooltip ("Hacerla ahora en la salida de video, sin cambiar de escena");
    tryButton.onClick = [this] { if (onTry) onTry (current()); };
    cancelButton.onClick = [this] { close(); };
    saveButton.getProperties().set (DarkLookAndFeel::filledProperty, true);
    saveButton.setColour (juce::TextButton::buttonColourId, DarkLookAndFeel::custom);
    saveButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff120e24));
    saveButton.onClick = [this]
    {
        if (onSave)
            onSave (current());
        close();
    };
    for (auto* b : { &tryButton, &cancelButton, &saveButton })
        addAndMakeVisible (b);

    setKind (CustomTransitionKind::stinger);
    setSize (540, 500);
}

TransitionDialog::~TransitionDialog() = default;

void TransitionDialog::show (juce::Component* parent, std::function<void (const CustomTransition&)> tryIt,
                             std::function<void (const CustomTransition&)> save, const CustomTransition* existing)
{
    auto content = std::make_unique<TransitionDialog>();
    content->onTry = std::move (tryIt);
    content->onSave = std::move (save);
    if (existing != nullptr)
        content->load (*existing);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (content.release());
    options.dialogTitle = existing != nullptr ? "Modificar transicion" : "Nueva transicion personalizada";
    options.dialogBackgroundColour = juce::Colour (0xff18181b);
    options.componentToCentreAround = parent;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    // Centrada taparia la salida de video (arriba en el centro): abajo a la izquierda, donde la
    // biblioteca, para ver la prueba.
    if (auto* window = options.launchAsync(); window != nullptr && parent != nullptr)
    {
        const auto area = parent->getScreenBounds();
        window->setTopLeftPosition (area.getX() + 12, juce::jmax (area.getY(), area.getBottom() - window->getHeight() - 12));
    }
}

void TransitionDialog::load (const CustomTransition& t)
{
    editingId = t.id;
    nameEditor.setText (t.name, false);
    setKind (t.kind);   // antes que el archivo: cambiar de tipo lo borra
    file = t.file;
    fileEditor.setText (file.getFileName(), false);
    durationSlider.setValue (t.milliseconds, juce::dontSendNotification);
    cutSlider.setValue (t.cutMilliseconds, juce::dontSendNotification);
    softnessSlider.setValue (t.softness * 100.0, juce::dontSendNotification);
    matteToggle.setToggleState (t.matte, juce::dontSendNotification);
    colour = t.colour;
    saveButton.setButtonText ("Guardar cambios");
    update();
    repaint();
}

CustomTransition TransitionDialog::current() const
{
    CustomTransition t;
    t.id = editingId;
    t.name = nameEditor.getText().trim();
    if (t.name.isEmpty())
        t.name = "Personalizada";
    t.kind = kind;
    t.file = file;
    t.milliseconds = (int) durationSlider.getValue();
    t.cutMilliseconds = juce::jmin ((int) cutSlider.getValue(), t.milliseconds);
    t.softness = (float) softnessSlider.getValue() / 100.0f;
    t.colour = colour;
    t.matte = kind == CustomTransitionKind::stinger && matteToggle.getToggleState();
    return t;
}

void TransitionDialog::setKind (CustomTransitionKind k)
{
    if (k != kind)
        file = {};
    kind = k;
    for (size_t i = 0; i < kinds.size(); ++i)
        kinds[i]->setToggleState ((int) i == (int) k, juce::dontSendNotification);
    const bool stinger = k == CustomTransitionKind::stinger, luma = k == CustomTransitionKind::luma, solid = k == CustomTransitionKind::colour;
    fileEditor.setVisible (! solid);
    browseButton.setVisible (! solid);
    cutSlider.setVisible (stinger);
    matteToggle.setVisible (stinger);
    softnessSlider.setVisible (luma);
    for (auto& s : swatches)
        s.setVisible (solid);
    otherColourButton.setVisible (solid);
    fileEditor.setTextToShowWhenEmpty (stinger ? "Elige un video (mp4, mov, wmv...)" : "Sin imagen: barrido en diagonal", DarkLookAndFeel::textFaint);
    fileEditor.setText (file.getFileName(), false);
    if (solid)
        durationSlider.setValue (juce::jmin (durationSlider.getValue(), 2000.0));
    update();
    resized();
    repaint();
}

void TransitionDialog::chooseFile()
{
    const bool stinger = kind == CustomTransitionKind::stinger;
    chooser = std::make_unique<juce::FileChooser> (stinger ? "Video del stinger" : "Imagen en grises del barrido",
                                                   juce::File::getSpecialLocation (stinger ? juce::File::userMoviesDirectory : juce::File::userPicturesDirectory),
                                                   stinger ? MediaFileSource::fileFilter() : juce::String ("*.png;*.jpg;*.jpeg;*.bmp;*.gif"));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, stinger] (const juce::FileChooser& fc)
                          {
                              const auto chosen = fc.getResult();
                              if (! chosen.existsAsFile())
                                  return;
                              if (stinger)
                              {
                                  // Un video con transparencia (.webm, .mov): se convierte a uno con mate y se usa ese.
                                  AlphaConvertDialog::prepare (chosen, [safe = juce::Component::SafePointer<TransitionDialog> (this)] (const juce::File& f, bool matte)
                                  {
                                      if (safe != nullptr)
                                          safe->useFile (f, true, matte);
                                  });
                                  return;
                              }
                              useFile (chosen, false, false);
                          });
}

void TransitionDialog::useFile (const juce::File& chosen, bool stinger, bool converted)
{
    file = chosen;
    fileEditor.setText (file.getFileName(), false);
    if (stinger)
    {
        // La duracion del video como duracion de la transicion; el cambio, a la mitad.
        MediaFoundation::initialiseThisThread();
        juce::String error;
        if (auto decoder = VideoDecoder::open (file, 64, error, false); decoder != nullptr && decoder->getDuration() > 0.0)
        {
            const double ms = juce::jlimit (100.0, 20000.0, decoder->getDuration() * 1000.0);
            durationSlider.setValue (ms, juce::dontSendNotification);
            cutSlider.setValue (std::round (ms / 2.0 / 50.0) * 50.0, juce::dontSendNotification);
            // Convertido aqui, o mas de 3 veces mas ancho que alto (2560x720, 3840x1080): un 16:9 con su mate al lado.
            matteToggle.setToggleState (converted || decoder->getWidth() > 3 * decoder->getHeight(), juce::dontSendNotification);
        }
        repaint();
    }
    update();
}

void TransitionDialog::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (auto* selector = dynamic_cast<juce::ColourSelector*> (source))
        setColour (selector->getCurrentColour());
}

void TransitionDialog::setColour (juce::Colour c)
{
    colour = c;
    repaint();
}

void TransitionDialog::update()
{
    const bool ready = nameEditor.getText().trim().isNotEmpty() && (kind != CustomTransitionKind::stinger || file.existsAsFile());
    saveButton.setEnabled (ready);
    tryButton.setEnabled (kind != CustomTransitionKind::stinger || file.existsAsFile());
}

void TransitionDialog::close()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState (0);   // se borra sola
}

void TransitionDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff18181b));
    g.setColour (DarkLookAndFeel::textQuiet);
    g.setFont (DarkLookAndFeel::sans (12.0f));
    g.drawText ("Nombre", nameLabel, juce::Justification::centredLeft, false);
    g.drawText ("Tipo", kindLabel, juce::Justification::centredLeft, false);
    if (fileEditor.isVisible())
        g.drawText (kind == CustomTransitionKind::stinger ? "Video" : "Imagen en grises (lo oscuro entra primero)", fileLabel,
                    juce::Justification::centredLeft, false);
    if (cutSlider.isVisible())
        g.drawText ("Punto de cambio", cutLabel, juce::Justification::centredLeft, false);
    if (softnessSlider.isVisible())
        g.drawText ("Suavidad del borde", softnessLabel, juce::Justification::centredLeft, false);
    g.drawText ("Duracion", durationLabel, juce::Justification::centredLeft, false);
    if (kind == CustomTransitionKind::colour)
    {
        g.drawText ("Color", colourLabel, juce::Justification::centredLeft, false);
        // El color elegido, junto a las muestras.
        const auto chosen = otherColourButton.getBounds().translated (otherColourButton.getWidth() + 10, 0).withWidth (34).toFloat();
        g.setColour (colour);
        g.fillRoundedRectangle (chosen, 6.0f);
        g.setColour (DarkLookAndFeel::text);
        g.drawRoundedRectangle (chosen.reduced (0.5f), 6.0f, 1.5f);
    }
    if (kind == CustomTransitionKind::stinger)
    {
        g.setColour (DarkLookAndFeel::textFaint);
        g.setFont (DarkLookAndFeel::sans (11.0f));
        g.drawFittedText (matteToggle.getToggleState() ? "Con mate se ve la escena por donde el video es transparente. Su sonido va al master."
                                                       : "Sin mate el video se ve entero (sin transparencia). Su sonido va al master.",
                          noteArea, juce::Justification::topLeft, 2);
    }
    g.setColour (DarkLookAndFeel::line);
    g.fillRect (16, saveButton.getY() - 14, getWidth() - 32, 1);
}

void TransitionDialog::resized()
{
    auto r = getLocalBounds().reduced (20, 16);
    nameLabel = r.removeFromTop (18);
    nameEditor.setBounds (r.removeFromTop (34));
    r.removeFromTop (12);
    kindLabel = r.removeFromTop (18);
    auto cards = r.removeFromTop (70);
    const int cw = (cards.getWidth() - 12) / 3;
    for (auto& k : kinds)
    {
        k->setBounds (cards.removeFromLeft (cw));
        cards.removeFromLeft (6);
    }
    r.removeFromTop (12);

    auto buttons = r.removeFromBottom (36);
    saveButton.setBounds (buttons.removeFromRight (150));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (90));
    tryButton.setBounds (buttons.removeFromLeft (180));
    r.removeFromBottom (24);

    if (fileEditor.isVisible())
    {
        fileLabel = r.removeFromTop (18);
        auto row = r.removeFromTop (34);
        browseButton.setBounds (row.removeFromRight (110));
        row.removeFromRight (8);
        fileEditor.setBounds (row);
        r.removeFromTop (matteToggle.isVisible() ? 6 : 12);
    }
    if (matteToggle.isVisible())
    {
        matteToggle.setBounds (r.removeFromTop (24));
        r.removeFromTop (8);
    }
    auto sliderRow = [&r] (juce::Rectangle<int>& label, juce::Slider& s)
    {
        label = r.removeFromTop (18);
        s.setBounds (r.removeFromTop (30));
        r.removeFromTop (10);
    };
    if (cutSlider.isVisible())
        sliderRow (cutLabel, cutSlider);
    if (softnessSlider.isVisible())
        sliderRow (softnessLabel, softnessSlider);
    if (kind == CustomTransitionKind::colour)
    {
        colourLabel = r.removeFromTop (18);
        auto row = r.removeFromTop (34);
        for (auto& s : swatches)
        {
            s.setBounds (row.removeFromLeft (34));
            row.removeFromLeft (6);
        }
        otherColourButton.setBounds (row.removeFromLeft (70));
        r.removeFromTop (12);
    }
    sliderRow (durationLabel, durationSlider);
    noteArea = r.removeFromTop (32);
}
}
