#include "engine/InstanceGraph.h"

#include "engine/AudioEngine.h"
#include "engine/GainProcessor.h"
#include "engine/MidiSourceProcessor.h"
#include "engine/PluginManager.h"

namespace vstplayer
{

GainProcessor* Instance::getGainProcessor() const noexcept
{
    return gainNode != nullptr ? dynamic_cast<GainProcessor*> (gainNode->getProcessor()) : nullptr;
}

MidiSourceProcessor* Instance::getMidiSource() const noexcept
{
    return midiNode != nullptr ? dynamic_cast<MidiSourceProcessor*> (midiNode->getProcessor()) : nullptr;
}

namespace
{
using Graph = juce::AudioProcessorGraph;
constexpr double kFallbackSampleRate = 48000.0;
constexpr int kFallbackBufferSize = 512;
} // namespace

InstanceGraph::InstanceGraph (AudioEngine& e, PluginManager& p)
    : engine (e), plugins (p)
{
}

InstanceGraph::~InstanceGraph()
{
    // Pending async loads hold only a weak reference: their plugin is discarded on arrival.
    for (auto& instance : instances)
        removeNodes (*instance);

    engine.getGraph().rebuild();

    for (auto& instance : instances)
    {
        instance->midiNode = nullptr;
        instance->gainNode = nullptr;
        instance->pluginNode = nullptr;
    }

    instances.clear();
}

InstanceId InstanceGraph::addInstance (const juce::PluginDescription& description)
{
    return addInstance (description, AddOptions {});
}

Instance& InstanceGraph::createEntry (const juce::PluginDescription& description, const AddOptions& options)
{
    auto instance = std::make_unique<Instance>();
    instance->id = options.id.isNotEmpty() && findInstance (options.id) == nullptr ? options.id : juce::Uuid().toDashedString();
    instance->name = makeUniqueName (options.name.trim().isNotEmpty() ? options.name.trim() : description.name);
    instance->description = description;
    instance->state = Instance::State::loading;
    instance->gainDb = juce::jlimit (GainProcessor::kMinGainDb, GainProcessor::kMaxGainDb, options.gainDb);
    instance->mute = options.mute;
    instance->window = options.window;
    instance->window.open = false; // only true while an editor window exists

    instances.push_back (std::move (instance));
    ++editCounter;
    return *instances.back();
}

InstanceId InstanceGraph::addInstance (const juce::PluginDescription& description, AddOptions options)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto id = createEntry (description, options).id;
    listeners.call ([] (Listener& l) { l.instanceListChanged(); });

    const auto sampleRate = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : kFallbackSampleRate;
    const auto bufferSize = engine.getBufferSize() > 0 ? engine.getBufferSize() : kFallbackBufferSize;

    juce::WeakReference<InstanceGraph> weakThis (this);

    plugins.getFormatManager().createPluginInstanceAsync (
        description, sampleRate, bufferSize,
        [weakThis, id, prepare = std::move (options.prepare)] (std::unique_ptr<juce::AudioPluginInstance> plugin,
                                                               const juce::String& error)
        {
            if (auto* self = weakThis.get())
                self->pluginCreated (id, std::move (plugin), error, prepare);
            // Otherwise the graph is gone and the plugin is destroyed here, on the message thread.
        });

    return id;
}

InstanceId InstanceGraph::addMissingInstance (const juce::PluginDescription& description, const AddOptions& options,
                                              const juce::String& message)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto& instance = createEntry (description, options);
    instance.state = Instance::State::missing;
    instance.errorMessage = message;
    const auto id = instance.id;

    listeners.call ([] (Listener& l) { l.instanceListChanged(); });
    return id;
}

void InstanceGraph::pluginCreated (const InstanceId& id, std::unique_ptr<juce::AudioPluginInstance> plugin,
                                   const juce::String& error, const std::function<void (juce::AudioPluginInstance&)>& prepare)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto* instance = findInstance (id);

    if (instance == nullptr)
        return; // removed while loading: drop the plugin

    if (plugin == nullptr)
    {
        instance->state = Instance::State::failed;
        instance->errorMessage = juce::String::fromUTF8 ("No se pudo cargar ") + instance->description.name
                                 + " (" + instance->description.fileOrIdentifier + ")"
                                 + (error.isNotEmpty() ? ": " + error : juce::String());
    }
    else
    {
        // Only the main buses: extra outputs (multi-out instruments) and sidechains are off.
        plugin->disableNonMainBuses();

        if (prepare)
            prepare (*plugin);

        const auto acceptsMidi = plugin->acceptsMidi();
        const auto numOutputs = plugin->getTotalNumOutputChannels();

        auto& graph = engine.getGraph();
        const auto mixerId = engine.getMixerNodeId();

        auto gain = std::make_unique<GainProcessor>();
        gain->setGainDb (instance->gainDb);
        gain->setMute (instance->mute);

        instance->midiNode = graph.addNode (std::make_unique<MidiSourceProcessor>(), std::nullopt, Graph::UpdateKind::async);
        instance->pluginNode = graph.addNode (std::move (plugin), std::nullopt, Graph::UpdateKind::async);
        instance->gainNode = graph.addNode (std::move (gain), std::nullopt, Graph::UpdateKind::async);

        if (instance->midiNode == nullptr || instance->pluginNode == nullptr || instance->gainNode == nullptr)
        {
            removeNodes (*instance);
            graph.rebuild();
            instance->midiNode = nullptr;
            instance->pluginNode = nullptr;
            instance->gainNode = nullptr;
            instance->state = Instance::State::failed;
            instance->errorMessage = juce::String::fromUTF8 ("No se pudo agregar el plugin al grafo de audio.");
        }
        else
        {
            const auto midiId = instance->midiNode->nodeID;
            const auto pluginId = instance->pluginNode->nodeID;
            const auto gainId = instance->gainNode->nodeID;
            const auto midiChannel = Graph::midiChannelIndex;

            if (acceptsMidi)
                graph.addConnection ({ { midiId, midiChannel }, { pluginId, midiChannel } }, Graph::UpdateKind::async);

            if (numOutputs >= 2)
            {
                graph.addConnection ({ { pluginId, 0 }, { gainId, 0 } }, Graph::UpdateKind::async);
                graph.addConnection ({ { pluginId, 1 }, { gainId, 1 } }, Graph::UpdateKind::async);
            }
            else if (numOutputs == 1)
            {
                graph.addConnection ({ { pluginId, 0 }, { gainId, 0 } }, Graph::UpdateKind::async);
                graph.addConnection ({ { pluginId, 0 }, { gainId, 1 } }, Graph::UpdateKind::async);
            }

            for (int ch = 0; ch < 2; ++ch)
                graph.addConnection ({ { gainId, ch }, { mixerId, ch } }, Graph::UpdateKind::async);

            graph.rebuild();

            instance->state = Instance::State::ready;
            instance->errorMessage.clear();
        }
    }

    listeners.call ([instance] (Listener& l) { l.instanceLoadFinished (*instance); });
    listeners.call ([] (Listener& l) { l.instanceListChanged(); });
}

void InstanceGraph::removeNodes (Instance& instance)
{
    auto& graph = engine.getGraph();

    for (auto* node : { &instance.gainNode, &instance.pluginNode, &instance.midiNode })
        if (*node != nullptr)
            graph.removeNode (node->get(), Graph::UpdateKind::async);
}

bool InstanceGraph::removeInstance (const InstanceId& id)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto find = [this, &id]
    {
        return std::find_if (instances.begin(), instances.end(), [&id] (auto& i) { return i->id == id; });
    };

    if (find() == instances.end())
        return false;

    // Editors must be gone before their processor is destroyed.
    {
        auto* instance = find()->get();
        listeners.call ([instance] (Listener& l) { l.instanceAboutToBeRemoved (*instance); });
    }

    // A listener may have changed the list: look the instance up again.
    const auto it = find();

    if (it == instances.end())
        return false;

    auto removed = std::move (*it);
    instances.erase (it);
    ++editCounter;

    removeNodes (*removed);
    // Synchronous rebuild: once it returns the audio thread no longer renders these nodes,
    // so dropping our references below destroys the plugin here, on the message thread.
    engine.getGraph().rebuild();

    removed->midiNode = nullptr;
    removed->gainNode = nullptr;
    removed->pluginNode = nullptr;
    removed.reset();

    listeners.call ([] (Listener& l) { l.instanceListChanged(); });
    return true;
}

void InstanceGraph::removeAllInstances()
{
    while (! instances.empty())
        removeInstance (instances.back()->id);
}

Instance* InstanceGraph::findInstance (const InstanceId& id) const
{
    for (auto& instance : instances)
        if (instance->id == id)
            return instance.get();

    return nullptr;
}

int InstanceGraph::getNumLoading() const
{
    int count = 0;

    for (auto& instance : instances)
        count += instance->state == Instance::State::loading ? 1 : 0;

    return count;
}

void InstanceGraph::setName (const InstanceId& id, const juce::String& newName)
{
    if (auto* instance = findInstance (id))
    {
        const auto trimmed = newName.trim();
        const auto resolvedName = trimmed.isNotEmpty() ? trimmed : instance->description.name;

        if (resolvedName == instance->name)
            return;

        instance->name = resolvedName;
        ++editCounter;
        listeners.call ([] (Listener& l) { l.instanceListChanged(); });
    }
}

void InstanceGraph::setGainDb (const InstanceId& id, float gainDb)
{
    if (auto* instance = findInstance (id))
    {
        const auto newGain = juce::jlimit (GainProcessor::kMinGainDb, GainProcessor::kMaxGainDb, gainDb);

        if (newGain != instance->gainDb)
            ++editCounter;

        instance->gainDb = newGain;

        if (auto* gain = instance->getGainProcessor())
            gain->setGainDb (instance->gainDb);
    }
}

void InstanceGraph::setMute (const InstanceId& id, bool shouldMute)
{
    if (auto* instance = findInstance (id))
    {
        if (instance->mute != shouldMute)
            ++editCounter;

        instance->mute = shouldMute;

        if (auto* gain = instance->getGainProcessor())
            gain->setMute (shouldMute);
    }
}

void InstanceGraph::setEditorWindowState (const InstanceId& id, const Instance::EditorWindowState& state)
{
    if (auto* instance = findInstance (id))
        instance->window = state;
}

bool InstanceGraph::sendMidi (const InstanceId& id, const juce::MidiMessage& message)
{
    if (auto* instance = findInstance (id))
    {
        if (auto* midi = instance->getMidiSource())
        {
            midi->addMessage (message);
            return true;
        }
    }

    return false;
}

void InstanceGraph::panic()
{
    for (auto& instance : instances)
        if (auto* midi = instance->getMidiSource())
            midi->requestPanic();
}

juce::String InstanceGraph::makeUniqueName (const juce::String& baseName) const
{
    const auto taken = [this] (const juce::String& candidate)
    {
        for (auto& instance : instances)
            if (instance->name == candidate)
                return true;

        return false;
    };

    if (! taken (baseName))
        return baseName;

    for (int n = 2;; ++n)
        if (const auto candidate = baseName + " " + juce::String (n); ! taken (candidate))
            return candidate;
}

} // namespace vstplayer
