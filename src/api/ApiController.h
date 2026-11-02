#pragma once

#include "engine/InstanceGraph.h"
#include "engine/Recorder.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vstplayer
{

class AudioEngine;
class MidiRouter;
class MidiSourceProcessor;
class ProjectController;

namespace api
{
class ApiServer;
}

// Local API for web pages (stage 7, protocol in docs/api.md): JSON commands over HTTP
// (POST /api) and WebSocket (/ws) on 127.0.0.1 only, plus events pushed to the WebSockets.
//
// Threading:
// - Requests arrive on network threads (ApiServer). Every command that reads or changes the
//   graph, the router or the project is posted to the message thread (MessageManager::callAsync)
//   and the network thread waits for its answer (loadProject answers once every plugin finished
//   loading).
// - setParam and midi run directly on the network thread: setValueNotifyingHost and the
//   instance's MidiMessageCollector are safe from any thread. The plugin / collector pointers
//   come from a table that the message thread updates under `liveLock` before an instance's
//   nodes are removed, so they are valid while the lock is held.
// - Events: parameter changes are flagged from any thread (AudioProcessorListener) and sent by a
//   20 Hz timer on the message thread, coalesced per instance (paramChanged). MIDI activity is
//   sampled by the same timer. instanceAdded / instanceRemoved / instanceLoaded / projectLoaded
//   come from the graph and project notifications. recordingStarted / recordingStopped come from
//   the Recorder (whoever started or stopped it: API, UI, device change).
class ApiController final : private InstanceGraph::Listener,
                            private Recorder::Listener,
                            private juce::AudioProcessorListener,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    struct Config
    {
        bool enabled = true;
        int port = 9471;
        juce::String token; // empty = no token
    };

    static constexpr int kDefaultPort = 9471;
    static constexpr const char* kHost = "127.0.0.1";

    ApiController (AudioEngine& engine, InstanceGraph& graph, MidiRouter& router, ProjectController& project);
    ~ApiController() override;

    // Starts (or restarts) the server. False if the port could not be opened (see getError()).
    bool start (const Config& config);
    // Stops the server; pending requests are answered with 503. Message thread.
    void stop();

    bool isRunning() const;
    int getNumClients() const;
    int getPort() const noexcept { return config.port; }
    juce::String getError() const { return error; }
    // Status bar text: "API: 127.0.0.1:9471, 2 clientes", "API: desactivada", "API: error …".
    juce::String getStatusText() const;

    // Status bar messages ("API: pánico", "API: proyecto abierto …").
    std::function<void (const juce::String& text, bool isError)> onStatusMessage;
    // Folder listed by listProjects when no "dir" is given (last project folder or Documents).
    std::function<juce::File()> getDefaultProjectFolder;
    // Panic of the UI (releases the on-screen keys too); default: MidiRouter::panic.
    std::function<void()> panicAction;
    // Gain / mute changed through the API: refresh the rows.
    std::function<void()> onInstanceValuesChanged;
    // startRecording: the options of settings.xml for the current project (format, WAV, folder).
    // Unset = MP3 256 kbps into Documents\VstPlayer\Grabaciones.
    std::function<Recorder::Options()> getRecordingOptions;

private:
    struct Live;

    //==============================================================================
    struct Reply
    {
        int status = 200;
        juce::var body; // DynamicObject
    };

    using Responder = std::function<void (Reply)>;
    using Command = void (ApiController::*) (const juce::var& request, const Responder& respond);

    static Reply ok (juce::DynamicObject::Ptr data = nullptr);
    static Reply fail (int status, const juce::String& code, const juce::String& message);

    // Network thread.
    std::string handleRequest (const std::string& body, int& status);
    Reply runOnMessageThread (Command command, const juce::var& request, int timeoutMs);
    std::shared_ptr<Live> findLiveLocked (const juce::String& idOrName) const;
    Reply setParamDirect (const juce::var& request);
    Reply midiDirect (const juce::var& request);

    // Message thread commands.
    void cmdStatus (const juce::var&, const Responder&);
    void cmdLoadProject (const juce::var&, const Responder&);
    void cmdListProjects (const juce::var&, const Responder&);
    void cmdSaveProject (const juce::var&, const Responder&);
    void cmdListParams (const juce::var&, const Responder&);
    void cmdGetParam (const juce::var&, const Responder&);
    void cmdSetGain (const juce::var&, const Responder&);
    void cmdSetMute (const juce::var&, const Responder&);
    void cmdPanic (const juce::var&, const Responder&);
    void cmdListPrograms (const juce::var&, const Responder&);
    void cmdSetProgram (const juce::var&, const Responder&);
    void cmdLevels (const juce::var&, const Responder&);
    void cmdAudioStats (const juce::var&, const Responder&);
    void cmdStartRecording (const juce::var&, const Responder&);
    void cmdStopRecording (const juce::var&, const Responder&);

    static void refreshParameterCache (juce::AudioPluginInstance& plugin);
    void refreshThen (const InstanceId& id, std::function<void()> then);
    Instance* findReadyInstance (const juce::var& request, Reply& error) const;
    juce::var describeInstance (const Instance& instance) const;
    juce::var describeProject() const;
    juce::var describeParameter (juce::AudioProcessorParameter& p, int index) const;
    static juce::var describeRecording (const Recorder::Info& info);
    static juce::DynamicObject::Ptr describeRecordingResult (const Recorder::Result& result);

    //==============================================================================
    // Instances whose plugin is in the graph (for the network-thread commands and the
    // parameter events).
    struct Live
    {
        InstanceId id;
        juce::String name; // instance display name (lookups by name)
        juce::AudioPluginInstance* plugin = nullptr;
        MidiSourceProcessor* midi = nullptr;
        std::vector<juce::String> paramNames;
        std::map<juce::String, int> indexByName; // first parameter with that name
        std::map<juce::String, int> indexById;   // VST3 parameter id
        int numParams = 0;
        std::unique_ptr<std::atomic<bool>[]> dirty; // parameter changed since the last event
        std::atomic<bool> anyDirty { false };
        std::atomic<juce::int64> apiMidiCount { 0 };
        juce::int64 lastActivity = -1;
    };

    void syncLive (const InstanceId& excluded = {});
    void removeLive (const InstanceId& id);

    // InstanceGraph::Listener
    void instanceListChanged() override;
    void instanceAboutToBeRemoved (const Instance&) override;
    void instanceLoadFinished (const Instance&) override;

    // AudioProcessorListener (any thread)
    void audioProcessorParameterChanged (juce::AudioProcessor*, int parameterIndex, float) override;
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}

    // Recorder::Listener
    void recordingStarted (const Recorder::Info&) override;
    void recordingStopped (const Recorder::Result&) override;

    // ProjectController (loads finished).
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void timerCallback() override;
    void sendEvent (const juce::String& name, juce::DynamicObject::Ptr data);
    void resolveLoadWaiters();
    Reply makeLoadReply() const;

    AudioEngine& engine;
    InstanceGraph& graph;
    MidiRouter& router;
    ProjectController& project;

    std::unique_ptr<api::ApiServer> server;
    Config config;
    juce::String error;
    std::atomic<bool> stopping { false };
    std::atomic<int> numClients { 0 };
    juce::WeakReference<ApiController> weakThis;

    juce::CriticalSection liveLock;                     // network threads + message thread
    std::map<InstanceId, std::shared_ptr<Live>> live;   // guarded by liveLock
    juce::SpinLock processorLock;                       // also taken on the audio thread
    std::map<juce::AudioProcessor*, std::shared_ptr<Live>> liveByProcessor; // guarded by processorLock

    std::map<InstanceId, Instance::State> knownStates; // message thread (instanceAdded / Removed)
    juce::uint64 lastLoadSerial = 0;
    std::vector<Responder> loadWaiters; // loadProject requests waiting for the load to finish

    static constexpr int kEventHz = 20;
    static constexpr int kMaxParamsPerEvent = 256;
    static constexpr int kDefaultTimeoutMs = 10000;
    static constexpr int kLoadTimeoutMs = 120000;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ApiController)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApiController)
};

} // namespace vstplayer
