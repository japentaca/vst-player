#pragma once

#include "engine/Instance.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace vstplayer
{

class AudioEngine;
class PluginManager;

// Plugin instances inside the AudioEngine graph.
// Per instance: MidiSourceProcessor -> plugin -> GainProcessor -> AudioEngine output mixer.
// Plugins with more than two outputs: non-main buses are disabled and only the first two
// channels of the main bus reach the mixer; a mono plugin feeds both sides.
//
// Threading: every method must be called on the message thread (the graph itself is safe
// with respect to the audio thread). sendMidi / panic only queue data for the audio thread.
//
// Loading: VST3 plugins have to be created on the message thread (JUCE creates them there even
// through createPluginInstanceAsync), so creating one blocks the UI for as long as the plugin
// takes. To keep the UI responsive, creations are queued and run one at a time, each one a short
// moment after the previous one finished (the UI repaints and handles input in between and the
// "cargando" rows / progress are drawn before the first one starts). The plugin's binary is
// loaded beforehand on a background thread (LoadLibrary), which takes the DLL loading and its
// static initialisation off the message thread.
class InstanceGraph final
{
public:
    InstanceGraph (AudioEngine& engine, PluginManager& plugins);
    ~InstanceGraph();

    struct Listener
    {
        virtual ~Listener() = default;
        // Something in the list changed: added, removed, loaded, failed, renamed.
        virtual void instanceListChanged() {}
        // Called before the nodes are removed: close editors here.
        virtual void instanceAboutToBeRemoved (const Instance&) {}
        // Asynchronous load finished (state is ready or failed).
        virtual void instanceLoadFinished (const Instance&) {}
    };

    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // Options for instances restored from a project.
    struct AddOptions
    {
        InstanceId id;      // empty = new UUID (also when the id is already taken)
        juce::String name;  // empty = plugin name (made unique)
        float gainDb = 0.0f;
        bool mute = false;
        Instance::EditorWindowState window; // position to reuse (open is ignored)
        // Called on the message thread once the plugin exists, before its nodes enter the
        // graph (restore the state here). Not called if the load fails.
        std::function<void (juce::AudioPluginInstance&)> prepare;
    };

    // Starts loading asynchronously; returns the new id right away (state = loading).
    InstanceId addInstance (const juce::PluginDescription& description);
    InstanceId addInstance (const juce::PluginDescription& description, AddOptions options);
    // An instance whose plugin could not be resolved: listed (state = missing) but never loaded.
    InstanceId addMissingInstance (const juce::PluginDescription& description, const AddOptions& options,
                                   const juce::String& message);
    bool removeInstance (const InstanceId& id);
    void removeAllInstances();

    const std::vector<std::unique_ptr<Instance>>& getInstances() const noexcept { return instances; }
    Instance* findInstance (const InstanceId& id) const;
    int getNumLoading() const;

    void setName (const InstanceId& id, const juce::String& newName);
    void setGainDb (const InstanceId& id, float gainDb);
    void setMute (const InstanceId& id, bool shouldMute);
    void setEditorWindowState (const InstanceId& id, const Instance::EditorWindowState& state);

    // Incremented by every change a project would save: add, remove, rename, gain, mute
    // (not the editor window state). Used for the "unsaved changes" flag.
    juce::uint64 getEditCounter() const noexcept { return editCounter; }

    // New instance with the same plugin, state (getStateInformation), gain and mute, named
    // "<name> (copia)". Empty id if the source is not ready. MIDI routes are not copied here.
    InstanceId duplicateInstance (const InstanceId& sourceId);

    // Loading diagnostics (UI / tests): time spent on the message thread by the last and the
    // slowest plugin creation (including the state restore and the graph rebuild), in ms.
    double getLastCreationMs() const noexcept { return lastCreationMs; }
    double getMaxCreationMs() const noexcept { return maxCreationMs; }
    double getLastPreloadMs() const noexcept { return lastPreloadMs; }
    // Time::getMillisecondCounterHiRes() when the last creation started on the message thread.
    double getLastCreationStartMs() const noexcept { return lastCreationStartMs; }
    bool isCreationPending() const noexcept { return creationInProgress || ! creationQueue.empty(); }

    // Queues a MIDI message for the instance (any channel). False if unknown or not loaded.
    bool sendMidi (const InstanceId& id, const juce::MidiMessage& message);
    // Sustain off + all notes off + all sound off on the 16 channels of every instance.
    void panic();

private:
    struct PendingCreation
    {
        InstanceId id;
        juce::PluginDescription description;
        std::function<void (juce::AudioPluginInstance&)> prepare;
    };

    void scheduleNextCreation (int delayMs);
    void startNextCreation();
    void createNow (PendingCreation creation);

    void pluginCreated (const InstanceId& id, std::unique_ptr<juce::AudioPluginInstance> plugin, const juce::String& error,
                        const std::function<void (juce::AudioPluginInstance&)>& prepare);
    Instance& createEntry (const juce::PluginDescription& description, const AddOptions& options);
    void removeNodes (Instance& instance);
    juce::String makeUniqueName (const juce::String& baseName) const;

    AudioEngine& engine;
    PluginManager& plugins;
    std::vector<std::unique_ptr<Instance>> instances;
    juce::ListenerList<Listener> listeners;
    juce::uint64 editCounter = 0;

    std::deque<PendingCreation> creationQueue;
    bool creationInProgress = false; // preloading or creating the front of the queue
    juce::ThreadPool preloadPool { 1 };
    double lastCreationMs = 0.0, maxCreationMs = 0.0, lastPreloadMs = 0.0, lastCreationStartMs = 0.0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (InstanceGraph)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstanceGraph)
};

} // namespace vstplayer
