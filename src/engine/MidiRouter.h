#pragma once

#include "engine/InstanceGraph.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <bitset>
#include <map>
#include <memory>
#include <vector>

namespace vstplayer
{

// One MIDI route: messages from inputDevice on any of `channels` go to the instance.
// remapChannel 0 = "keep" (the original channel passes through), 1..16 = "remapTo: n".
struct MidiRoute
{
    juce::String inputDevice; // port name as reported by MidiInput::getAvailableDevices()
    juce::uint16 channelMask = 0xffff; // bit (n - 1) = channel n
    InstanceId instanceId;
    int remapChannel = 0;

    static constexpr juce::uint16 kAllChannels = 0xffff;

    bool hasChannel (int channel) const noexcept { return channel >= 1 && channel <= 16 && (channelMask & (1u << (channel - 1))) != 0; }
    bool isKeep() const noexcept { return remapChannel < 1 || remapChannel > 16; }

    // "todos", "ninguno" or "1, 2, 5" (Spanish, UI text).
    static juce::String describeChannels (juce::uint16 mask);

    bool operator== (const MidiRoute& o) const noexcept
    {
        return inputDevice == o.inputDevice && channelMask == o.channelMask && instanceId == o.instanceId
               && remapChannel == o.remapChannel;
    }
};

// Opens the MIDI input ports the routes use and delivers their messages to the instances.
//
// - Ports are matched by name. The device list is watched (MidiDeviceListConnection plus a 2 s
//   poll as a fallback): a port that disappears is closed, and reopened when it comes back
//   (e.g. a loopMIDI port deleted and created again). Its routes stay meanwhile.
// - Delivery: the MidiInputCallback (MIDI thread) writes into the instance's
//   MidiSourceProcessor (a MidiMessageCollector), which hands the plugin its messages in the
//   next audio block at offsets derived from the input timestamps.
// - Only channel voice messages pass (0x80..0xEF); system messages (sysex, clock, active
//   sensing...) are dropped.
// - Hanging notes: the router tracks held notes per instance and output channel. A note-on for a
//   note that is already held (retrigger without note-off, or two source channels remapped to
//   one) gets a note-off right before it. When an instance's routes change or its port goes away,
//   its held notes are released. panic() clears that state and every collector.
//
// Threading: everything public is message-thread only, except what the doc says otherwise.
class MidiRouter final : private InstanceGraph::Listener,
                         private juce::MidiInputCallback,
                         private juce::Timer,
                         public juce::ChangeBroadcaster // ports or routes changed (async)
{
public:
    explicit MidiRouter (InstanceGraph& graph);
    ~MidiRouter() override;

    //==============================================================================
    const std::vector<MidiRoute>& getRoutes() const noexcept { return routes; }
    void setRoutes (std::vector<MidiRoute> newRoutes);
    std::vector<MidiRoute> getRoutesForInstance (const InstanceId& id) const;
    // Replaces every route of that instance (empty = no MIDI input).
    void setRoutesForInstance (const InstanceId& id, std::vector<MidiRoute> instanceRoutes);

    // Incremented whenever the routes change (for the "unsaved changes" flag).
    juce::uint64 getEditCounter() const noexcept { return editCounter; }

    // Releases held notes, then all notes off + all sound off on every instance (collectors
    // are emptied by the instances' MidiSourceProcessor).
    void panic();

    //==============================================================================
    struct PortStatus
    {
        juce::String name;
        juce::String identifier;
        bool available = false; // present in the system device list
        bool open = false;      // opened and started by us
        juce::String error;     // last open error
        int numRoutes = 0;
        juce::int64 messagesReceived = 0;
    };

    // Every available input port plus the ones routes reference but are missing.
    std::vector<PortStatus> getPortStatus() const;
    juce::StringArray getAvailableInputNames() const;
    bool isPortOpen (const juce::String& name) const;

    // Keep every input port open (diagnostic window) even without routes.
    void setMonitorAllPorts (bool shouldMonitor);
    bool isMonitoringAllPorts() const noexcept { return monitorAll; }

    // Re-reads the device list and opens / closes ports. Called automatically.
    void refreshDevices();

    //==============================================================================
    // Last received messages (all open ports, including dropped ones). Any thread.
    struct LogEntry
    {
        juce::uint64 serial = 0;
        double time = 0.0; // seconds, Time::getMillisecondCounterHiRes() base
        juce::String device;
        juce::MidiMessage message;
        int numTargets = 0; // instances it was delivered to
        bool dropped = false; // system message
    };

    std::vector<LogEntry> getRecentMessages() const;
    juce::uint64 getLogSerial() const noexcept { return logSerial.load(); }

    // Messages routed to the instance so far (MIDI activity LED). Any thread.
    juce::int64 getActivityCount (const InstanceId& id) const;

    static constexpr int kLogSize = 200;
    static constexpr int kPollIntervalMs = 2000;

private:
    struct Target
    {
        MidiSourceProcessor* midi = nullptr;
        std::array<std::bitset<128>, 16> held; // by output channel
        juce::int64 activity = 0;
    };

    struct DispatchEntry
    {
        juce::uint16 channelMask = 0;
        int remapChannel = 0;
        Target* target = nullptr;
    };

    struct OpenPort
    {
        juce::String name;
        juce::String identifier;
        std::unique_ptr<juce::MidiInput> input;
    };

    // InstanceGraph::Listener
    void instanceListChanged() override;
    void instanceAboutToBeRemoved (const Instance&) override;
    void instanceLoadFinished (const Instance&) override;

    // MidiInputCallback (MIDI thread)
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

    void timerCallback() override;

    void syncTargets (const InstanceId& excluded = {});
    void rebuildDispatch();
    void deliverLocked (Target& target, const juce::MidiMessage& message);
    void releaseHeldLocked (Target& target);
    void releaseHeldNotesOf (const juce::StringArray& instanceIds);
    void closePort (const juce::String& name);
    void addLog (const juce::String& device, const juce::MidiMessage& message, int numTargets, bool dropped);
    static juce::String deviceListSignature (const juce::Array<juce::MidiDeviceInfo>& devices);

    InstanceGraph& graph;
    std::vector<MidiRoute> routes;
    juce::uint64 editCounter = 0;
    bool monitorAll = false;

    // Message thread only.
    std::vector<std::unique_ptr<OpenPort>> openPorts;
    std::map<juce::String, juce::String> openErrors; // port name -> last error
    juce::String lastDeviceSignature;
    juce::Array<juce::MidiDeviceInfo> lastDevices;
    juce::MidiDeviceListConnection deviceListConnection;

    // Shared with the MIDI threads, guarded by `lock`.
    juce::CriticalSection lock;
    std::map<InstanceId, std::unique_ptr<Target>> targets;
    std::map<juce::MidiInput*, std::vector<DispatchEntry>> dispatch;
    std::map<juce::MidiInput*, juce::String> sourceNames;
    std::map<juce::String, juce::int64> receivedByPort;

    // Log ring buffer, guarded by `logLock`.
    juce::CriticalSection logLock;
    std::vector<LogEntry> log;
    size_t logNext = 0;
    std::atomic<juce::uint64> logSerial { 0 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (MidiRouter)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRouter)
};

} // namespace vstplayer
