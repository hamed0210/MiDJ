#include "Control/MidiController.h"

namespace midj
{
MidiController::MidiController (ControlSurface& s) : surface (s) {}

MidiController::~MidiController()
{
    *alive = false;
    stopTimer();
    for (auto& in : inputs)
        in->stop();
}

juce::File MidiController::getDefaultMappingFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("MiDJ").getChildFile ("midi.xml");
}

void MidiController::loadMapping (const juce::File& file)
{
    mappingFile = file;
    const juce::ScopedLock sl (lock);
    mapping.load (file);
}

void MidiController::startDevices()
{
    refreshDevices();
    startTimer (2000);
}

void MidiController::refreshDevices()
{
    const auto available = juce::MidiInput::getAvailableDevices();

    // Cerrar las que ya no estan.
    inputs.erase (std::remove_if (inputs.begin(), inputs.end(),
                                  [&] (const std::unique_ptr<juce::MidiInput>& in)
                                  {
                                      const bool gone = std::none_of (available.begin(), available.end(),
                                          [&] (const juce::MidiDeviceInfo& d) { return d.identifier == in->getIdentifier(); });
                                      if (gone)
                                          in->stop();
                                      return gone;
                                  }),
                  inputs.end());

    // Abrir las nuevas (si otra aplicacion la tiene en uso, Windows no deja: se reintenta luego).
    bool changed = false;
    for (const auto& d : available)
    {
        const bool open = std::any_of (inputs.begin(), inputs.end(),
                                       [&] (const std::unique_ptr<juce::MidiInput>& in) { return in->getIdentifier() == d.identifier; });
        if (open)
            continue;
        if (auto in = juce::MidiInput::openDevice (d.identifier, this))
        {
            in->start();
            inputs.push_back (std::move (in));
            changed = true;
        }
    }
    if (changed)
        sendChangeMessage();
}

juce::StringArray MidiController::getDeviceNames() const
{
    juce::StringArray names;
    for (const auto& in : inputs)
        names.add (in->getName());
    return names;
}

void MidiController::startLearning (const juce::String& actionId)
{
    {
        const juce::ScopedLock sl (lock);
        learning = actionId;
    }
    sendChangeMessage();
}

void MidiController::stopLearning()
{
    {
        const juce::ScopedLock sl (lock);
        learning.clear();
    }
    sendChangeMessage();
}

juce::String MidiController::getLearningAction() const
{
    const juce::ScopedLock sl (lock);
    return learning;
}

void MidiController::removeAction (const juce::String& actionId)
{
    {
        const juce::ScopedLock sl (lock);
        mapping.removeAction (actionId);
    }
    saveSoon();
    sendChangeMessage();
}

juce::String MidiController::describeBindings (const juce::String& actionId) const
{
    const juce::ScopedLock sl (lock);
    juce::StringArray parts;
    for (const auto& b : mapping.bindingsFor (actionId))
        parts.add (b.describe());
    return parts.joinIntoString (", ");
}

MidiMapping MidiController::getMappingCopy() const
{
    const juce::ScopedLock sl (lock);
    return mapping;
}

juce::String MidiController::getLastMessageText() const
{
    const juce::ScopedLock sl (lock);
    return lastMessage;
}

void MidiController::saveSoon()
{
    // Guardar en el hilo de interfaz (el disco nunca desde el hilo MIDI).
    juce::MessageManager::callAsync ([this, flag = alive]
    {
        if (! flag->load())
            return;
        const juce::ScopedLock sl (lock);
        mapping.save (mappingFile);
    });
}

void MidiController::handleMessage (const juce::MidiMessage& m)
{
    auto control = MidiBinding::controlOf (m);
    if (! control)
        return;

    const int value = control->kind == MidiBinding::Kind::note ? m.getVelocity()
                    : control->kind == MidiBinding::Kind::cc   ? m.getControllerValue()
                                                                : m.getPitchWheelValue();
    const bool press = (control->kind == MidiBinding::Kind::note && m.isNoteOn())
                    || (control->kind == MidiBinding::Kind::cc && value > 0);

    MidiBinding binding;
    bool learned = false, found = false;
    {
        const juce::ScopedLock sl (lock);
        lastMessage = control->describe() + " = " + juce::String (value);

        if (learning.isNotEmpty())
        {
            const auto* info = Actions::find (learning);
            if (info == nullptr)
                return;
            // Cada tipo de accion aprende solo de controles que le sirven.
            const bool usable = info->type == ActionType::button   ? press
                              : info->type == ActionType::absolute ? control->kind != MidiBinding::Kind::note
                                                                   : control->kind == MidiBinding::Kind::cc;
            if (! usable)
                return;
            control->action = learning;
            if (info->type == ActionType::relative)
                // Un encoder desplazado manda valores cerca de 64 (63/65); en complemento a dos, cerca de 0/127.
                control->encoding = value >= 56 && value <= 72 ? MidiBinding::Encoding::relativeOffset
                                                               : MidiBinding::Encoding::relativeTwos;
            mapping.bind (*control);
            learning.clear();
            learned = true;
        }
        else if (const auto* b = mapping.find (*control))
        {
            binding = *b;
            found = true;
        }
    }

    if (learned)
    {
        saveSoon();
        sendChangeMessage();
        return;
    }
    if (! found)
    {
        sendChangeMessage();   // para que la interfaz muestre el ultimo mensaje recibido
        return;
    }

    const auto* info = Actions::find (binding.action);
    if (info == nullptr)
        return;
    switch (info->type)
    {
        case ActionType::button:
            if (press)
                surface.pressButton (binding.action);
            else
                surface.releaseButton (binding.action);   // nota soltada o CC a 0
            break;
        case ActionType::absolute:
            surface.setAbsolute (binding.action, binding.kind == MidiBinding::Kind::pitchBend ? MidiBinding::unitFromPitchBend (value)
                                                                                              : MidiBinding::unitFromCC (value));
            break;
        case ActionType::relative:
            surface.moveRelative (binding.action, MidiBinding::ticksFrom (value, binding.encoding));
            break;
    }
}
}
