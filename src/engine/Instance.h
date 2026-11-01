#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace vstplayer
{

class GainProcessor;
class MidiSourceProcessor;

using InstanceId = juce::String; // UUID, dashed form

// One loaded plugin and its nodes in the graph:
//   midiNode (MidiSourceProcessor) -> pluginNode -> gainNode (GainProcessor) -> output mixer.
// Owned by InstanceGraph; only touched on the message thread.
struct Instance
{
    enum class State
    {
        loading, // createPluginInstanceAsync in progress, no nodes yet
        ready,   // nodes in the graph
        failed   // could not be created, see errorMessage
    };

    InstanceId id;
    juce::String name; // editable display name
    juce::PluginDescription description;

    State state = State::loading;
    juce::String errorMessage;

    juce::AudioProcessorGraph::Node::Ptr midiNode;
    juce::AudioProcessorGraph::Node::Ptr pluginNode;
    juce::AudioProcessorGraph::Node::Ptr gainNode;

    float gainDb = 0.0f;
    bool mute = false;

    // Editor window state, kept while the window is closed so it reopens in the same place.
    struct EditorWindowState
    {
        bool open = false;
        bool hasPosition = false;
        juce::Point<int> position;
    };

    EditorWindowState window;

    bool isReady() const noexcept { return state == State::ready && pluginNode != nullptr; }

    juce::AudioPluginInstance* getPlugin() const noexcept
    {
        return pluginNode != nullptr ? dynamic_cast<juce::AudioPluginInstance*> (pluginNode->getProcessor()) : nullptr;
    }

    GainProcessor* getGainProcessor() const noexcept;
    MidiSourceProcessor* getMidiSource() const noexcept;
};

} // namespace vstplayer
