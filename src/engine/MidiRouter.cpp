#include "engine/MidiRouter.h"

#include "engine/MidiSourceProcessor.h"

#include <algorithm>

namespace vstplayer
{

juce::String MidiRoute::describeChannels (juce::uint16 mask)
{
    if (mask == kAllChannels)
        return "todos";

    if (mask == 0)
        return "ninguno";

    juce::StringArray list;

    for (int channel = 1; channel <= 16; ++channel)
        if ((mask & (1u << (channel - 1))) != 0)
            list.add (juce::String (channel));

    return list.joinIntoString (", ");
}

//==============================================================================
MidiRouter::MidiRouter (InstanceGraph& g)
    : graph (g)
{
    log.resize ((size_t) kLogSize);
    graph.addListener (this);

    juce::WeakReference<MidiRouter> weakThis (this);
    deviceListConnection = juce::MidiDeviceListConnection::make ([weakThis]
    {
        if (auto* self = weakThis.get())
            self->refreshDevices();
    });

    syncTargets();
    refreshDevices();
    startTimer (kPollIntervalMs);
}

MidiRouter::~MidiRouter()
{
    stopTimer();
    deviceListConnection.reset();
    graph.removeListener (this);

    while (! openPorts.empty())
        closePort (openPorts.back()->name);

    const juce::ScopedLock sl (lock);
    dispatch.clear();
    targets.clear();
}

//==============================================================================
void MidiRouter::setRoutes (std::vector<MidiRoute> newRoutes)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (newRoutes == routes)
        return;

    routes = std::move (newRoutes);
    ++editCounter;
    refreshDevices();

    // Notes held through the old routes would never get their note-off. Released only after
    // refreshDevices() swapped in the new dispatch table: until then the MIDI thread still
    // delivers (and marks as held) note-ons through the old routes.
    const juce::ScopedLock sl (lock);

    for (auto& [id, target] : targets)
        releaseHeldLocked (*target);
}

std::vector<MidiRoute> MidiRouter::getRoutesForInstance (const InstanceId& id) const
{
    std::vector<MidiRoute> result;

    for (auto& route : routes)
        if (route.instanceId == id)
            result.push_back (route);

    return result;
}

void MidiRouter::setRoutesForInstance (const InstanceId& id, std::vector<MidiRoute> instanceRoutes)
{
    JUCE_ASSERT_MESSAGE_THREAD

    for (auto& route : instanceRoutes)
        route.instanceId = id;

    if (getRoutesForInstance (id) == instanceRoutes)
        return;

    routes.erase (std::remove_if (routes.begin(), routes.end(), [&id] (const MidiRoute& r) { return r.instanceId == id; }),
                  routes.end());

    for (auto& route : instanceRoutes)
        routes.push_back (route);

    ++editCounter;

    refreshDevices();

    // Notes held through the old routes would never get their note-off: release them, after the
    // new dispatch table is in place (see setRoutes).
    releaseHeldNotesOf ({ id });
}

void MidiRouter::panic()
{
    {
        const juce::ScopedLock sl (lock);

        for (auto& [id, target] : targets)
            for (auto& channel : target->held)
                channel.reset();
    }

    graph.panic(); // all notes off + all sound off; drops what the collectors still hold
}

//==============================================================================
std::vector<MidiRouter::PortStatus> MidiRouter::getPortStatus() const
{
    std::vector<PortStatus> result;

    const auto findOrAdd = [&result] (const juce::String& name) -> PortStatus&
    {
        for (auto& status : result)
            if (status.name == name)
                return status;

        result.push_back ({});
        result.back().name = name;
        return result.back();
    };

    for (auto& device : lastDevices)
    {
        auto& status = findOrAdd (device.name);
        status.available = true;
        status.identifier = device.identifier;
    }

    for (auto& route : routes)
        if (route.inputDevice.isNotEmpty())
            ++findOrAdd (route.inputDevice).numRoutes;

    for (auto& port : openPorts)
        findOrAdd (port.get()->name).open = true;

    for (auto& [name, error] : openErrors)
        findOrAdd (name).error = error;

    const juce::ScopedLock sl (lock);

    for (auto& status : result)
        if (auto it = receivedByPort.find (status.name); it != receivedByPort.end())
            status.messagesReceived = it->second;

    return result;
}

juce::StringArray MidiRouter::getAvailableInputNames() const
{
    juce::StringArray names;

    for (auto& device : lastDevices)
        names.addIfNotAlreadyThere (device.name);

    return names;
}

bool MidiRouter::isPortOpen (const juce::String& name) const
{
    for (auto& port : openPorts)
        if (port->name == name)
            return true;

    return false;
}

void MidiRouter::setMonitorAllPorts (bool shouldMonitor)
{
    if (monitorAll != shouldMonitor)
    {
        monitorAll = shouldMonitor;
        refreshDevices();
    }
}

//==============================================================================
juce::String MidiRouter::deviceListSignature (const juce::Array<juce::MidiDeviceInfo>& devices)
{
    juce::String signature;

    for (auto& device : devices)
        signature << device.name << '\x01' << device.identifier << '\x02';

    return signature;
}

void MidiRouter::refreshDevices()
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto devices = juce::MidiInput::getAvailableDevices();
    lastDevices = devices;
    lastDeviceSignature = deviceListSignature (devices);

    juce::StringArray wanted;

    for (auto& route : routes)
        if (route.inputDevice.isNotEmpty())
            wanted.addIfNotAlreadyThere (route.inputDevice);

    if (monitorAll)
        for (auto& device : devices)
            wanted.addIfNotAlreadyThere (device.name);

    const auto findDevice = [&devices] (const juce::String& name) -> const juce::MidiDeviceInfo*
    {
        for (auto& device : devices)
            if (device.name == name)
                return &device;

        return nullptr;
    };

    // Close ports that are gone (or were recreated with another identifier) or no longer used.
    juce::StringArray toClose;

    for (auto& port : openPorts)
    {
        const auto* device = findDevice (port->name);

        if (device == nullptr || device->identifier != port->identifier || ! wanted.contains (port->name))
            toClose.add (port->name);
    }

    for (auto& name : toClose)
        closePort (name);

    // Open the wanted ports that exist.
    for (auto& name : wanted)
    {
        if (isPortOpen (name))
            continue;

        const auto* device = findDevice (name);

        if (device == nullptr)
        {
            openErrors.erase (name); // simply not connected: shown as such
            continue;
        }

        auto input = juce::MidiInput::openDevice (device->identifier, this);

        if (input == nullptr)
        {
            openErrors[name] = juce::String::fromUTF8 ("no se pudo abrir (¿lo usa otra aplicación?)");
            continue;
        }

        openErrors.erase (name);

        {
            const juce::ScopedLock sl (lock);
            sourceNames[input.get()] = name;
        }

        auto port = std::make_unique<OpenPort>();
        port->name = name;
        port->identifier = device->identifier;
        port->input = std::move (input);
        port->input->start();
        openPorts.push_back (std::move (port));
    }

    rebuildDispatch();
    sendChangeMessage();
}

void MidiRouter::closePort (const juce::String& name)
{
    const auto it = std::find_if (openPorts.begin(), openPorts.end(), [&name] (auto& p) { return p->name == name; });

    if (it == openPorts.end())
        return;

    auto port = std::move (*it);
    openPorts.erase (it);

    juce::StringArray affected;

    for (auto& route : routes)
        if (route.inputDevice == name)
            affected.addIfNotAlreadyThere (route.instanceId);

    {
        // From here on the callback ignores this source.
        const juce::ScopedLock sl (lock);
        dispatch.erase (port->input.get());
        sourceNames.erase (port->input.get());
    }

    // Notes that came through this port will never get their note-off.
    releaseHeldNotesOf (affected);

    // Outside the lock: stop() waits for a callback in progress, which takes the lock.
    port->input->stop();
    port->input = nullptr;
}

void MidiRouter::timerCallback()
{
    const auto devices = juce::MidiInput::getAvailableDevices();
    auto needsRefresh = deviceListSignature (devices) != lastDeviceSignature;

    // Retry ports that exist but could not be opened.
    if (! needsRefresh)
        for (auto& route : routes)
            if (route.inputDevice.isNotEmpty() && ! isPortOpen (route.inputDevice))
                for (auto& device : devices)
                    if (device.name == route.inputDevice)
                        needsRefresh = true;

    if (needsRefresh)
        refreshDevices();
}

//==============================================================================
void MidiRouter::syncTargets (const InstanceId& excluded)
{
    const juce::ScopedLock sl (lock);

    // Drop targets of instances that are gone, not ready, or about to be removed.
    for (auto it = targets.begin(); it != targets.end();)
    {
        auto* instance = graph.findInstance (it->first);
        const auto keep = it->first != excluded && instance != nullptr && instance->getMidiSource() == it->second->midi;
        it = keep ? std::next (it) : targets.erase (it);
    }

    for (auto& instance : graph.getInstances())
    {
        if (instance->id == excluded || targets.count (instance->id) != 0)
            continue;

        if (auto* midi = instance->getMidiSource())
        {
            auto target = std::make_unique<Target>();
            target->midi = midi;
            targets[instance->id] = std::move (target);
        }
    }

    rebuildDispatch(); // same lock scope: no dangling Target* in the dispatch table
}

void MidiRouter::rebuildDispatch()
{
    std::map<juce::MidiInput*, std::vector<DispatchEntry>> table;

    const juce::ScopedLock sl (lock);

    for (auto& route : routes)
    {
        const auto target = targets.find (route.instanceId);

        if (target == targets.end() || route.channelMask == 0)
            continue;

        for (auto& port : openPorts)
            if (port->name == route.inputDevice)
                table[port->input.get()].push_back ({ route.channelMask, route.isKeep() ? 0 : route.remapChannel, target->second.get() });
    }

    dispatch.swap (table);
}

void MidiRouter::releaseHeldLocked (Target& target)
{
    for (int index = 0; index < 16; ++index)
    {
        auto& held = target.held[(size_t) index];

        if (held.none())
            continue;

        for (int note = 0; note < 128; ++note)
            if (held.test ((size_t) note))
                target.midi->addMessage (juce::MidiMessage::noteOff (index + 1, note));

        held.reset();
    }
}

void MidiRouter::releaseHeldNotesOf (const juce::StringArray& instanceIds)
{
    const juce::ScopedLock sl (lock);

    for (auto& id : instanceIds)
        if (auto it = targets.find (id); it != targets.end())
            releaseHeldLocked (*it->second);
}

//==============================================================================
void MidiRouter::instanceListChanged()
{
    syncTargets();
}

void MidiRouter::instanceLoadFinished (const Instance&)
{
    syncTargets();
}

void MidiRouter::instanceAboutToBeRemoved (const Instance& instance)
{
    const auto id = instance.id;
    const auto before = routes.size();
    routes.erase (std::remove_if (routes.begin(), routes.end(), [&id] (const MidiRoute& r) { return r.instanceId == id; }),
                  routes.end());

    syncTargets (id); // its processor is about to be destroyed: stop writing to it now

    if (routes.size() != before)
        refreshDevices(); // closes ports nobody uses any more
}

//==============================================================================
void MidiRouter::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message)
{
    const auto* raw = message.getRawData();
    const auto status = message.getRawDataSize() > 0 ? raw[0] : 0;
    const auto dropped = status < 0x80 || status >= 0xf0; // keep channel voice messages only

    juce::String device;
    int numTargets = 0;

    {
        const juce::ScopedLock sl (lock);

        if (auto it = sourceNames.find (source); it != sourceNames.end())
        {
            device = it->second;
            ++receivedByPort[device];
        }

        if (! dropped)
        {
            if (auto it = dispatch.find (source); it != dispatch.end())
            {
                const auto channel = message.getChannel();

                for (auto& entry : it->second)
                {
                    if ((entry.channelMask & (1u << (channel - 1))) == 0)
                        continue;

                    auto out = message;

                    if (entry.remapChannel != 0)
                        out.setChannel (entry.remapChannel);

                    deliverLocked (*entry.target, out);
                    ++numTargets;
                }
            }
        }
    }

    addLog (device, message, numTargets, dropped);
}

void MidiRouter::deliverLocked (Target& target, const juce::MidiMessage& message)
{
    const auto index = (size_t) (message.getChannel() - 1);

    if (index < 16)
    {
        auto& held = target.held[index];

        if (message.isNoteOn())
        {
            const auto note = (size_t) message.getNoteNumber();

            // Retrigger without note-off: end the sounding note first so none is left hanging.
            if (held.test (note))
                target.midi->addMessage (juce::MidiMessage::noteOff ((int) index + 1, (int) note).withTimeStamp (message.getTimeStamp()));

            held.set (note);
        }
        else if (message.isNoteOff())
        {
            held.reset ((size_t) message.getNoteNumber());
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            held.reset();
        }
    }

    target.midi->addMessage (message);
    ++target.activity;
}

juce::int64 MidiRouter::getActivityCount (const InstanceId& id) const
{
    const juce::ScopedLock sl (lock);
    const auto it = targets.find (id);
    return it != targets.end() ? it->second->activity : 0;
}

//==============================================================================
void MidiRouter::addLog (const juce::String& device, const juce::MidiMessage& message, int numTargets, bool dropped)
{
    const juce::ScopedLock sl (logLock);
    auto& entry = log[logNext % log.size()];
    entry.serial = ++logSerial;
    entry.time = message.getTimeStamp();
    entry.device = device;
    entry.message = message;
    entry.numTargets = numTargets;
    entry.dropped = dropped;
    ++logNext;
}

std::vector<MidiRouter::LogEntry> MidiRouter::getRecentMessages() const
{
    const juce::ScopedLock sl (logLock);
    std::vector<LogEntry> result;
    const auto count = std::min (logNext, log.size());

    for (size_t i = logNext - count; i < logNext; ++i)
        result.push_back (log[i % log.size()]);

    return result;
}

} // namespace vstplayer
