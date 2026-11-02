#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <bitset>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace vstplayer
{

class AppSettings;
class AudioEngine;
class InstanceGraph;
class MidiRouter;
class MidiSourceProcessor;
class PluginManager;
class ProjectController;

// "--render": offline render of a project playing a Standard MIDI File, to a WAV.
//
//   VstPlayer.exe --render <project.vstproj.json> --midi <file.mid> --out <file.wav>
//                 [--sample-rate 44100] [--block 512] [--warmup 2] [--tail-max 60] [--data-dir <dir>]
//
// - No audio device, no MIDI ports, no API, no window. The project is loaded as the app does it
//   (ProjectController: plugins through plugins.xml, saved state, `params`, gains, mute, MIDI
//   routes), with read-only settings / plugin list: nothing is written to the data folder.
// - Every instance must load; a missing or failed plugin is an error (exit 1).
// - The graph runs non-realtime (setNonRealtime) on a thread of its own, as fast as the plugins
//   allow. First `--warmup` seconds of silence are processed at real-time pace and discarded
//   (plugins that load samples in the background), then the MIDI file: its time 0 is the first
//   sample of the WAV, every message is delivered at its exact sample inside the block.
// - Routing as live port input, with the routes of every input device: a message on channel c
//   goes once to each (instance, output channel) whose route takes c (keep or remapTo), the same
//   table carrilion's VstPlayer port builds. Held-note tracking as the live router (a retrigger
//   gets a note-off first). CC7 / CC10 are applied by the instance's gain node and not passed to
//   the plugin, as live. Meta, sysex and system messages are ignored.
// - Tail: after the last MIDI event, rendering goes on until the mix has stayed below -70 dBFS
//   for kSilenceHoldSeconds (at most --tail-max seconds); the file ends 0.5 s after the last
//   sample above -70 dBFS, never before the last event (carrilion's render rule).
// - Output: stereo 32-bit float WAV of the output mixer, written to a temporary file next to the
//   target and renamed at the end. stdout: one line of JSON (summary or error); progress on stderr.
//   Exit codes: 0 ok, 1 render error (project, plugins, writing), 2 bad arguments / unreadable input.
//
// Message thread: setup, project load, prepareToPlay, final rename. Render thread: processBlock
// and the WAV writer. Nothing touches the graph from the message thread while rendering.
class OfflineRender final : private juce::Timer
{
public:
    explicit OfflineRender (const juce::StringArray& args);
    ~OfflineRender() override;

    // Called once, on the message thread, with the process exit code.
    std::function<void (int exitCode)> onFinished;

    void start();
    bool isFinished() const noexcept { return finished; }

    static constexpr double kSilenceThreshold = 0.000316; // -70 dBFS
    static constexpr double kSilencePadSeconds = 0.5;
    static constexpr double kSilenceHoldSeconds = 2.0;
    static constexpr double kLoadTimeoutSeconds = 120.0;

    static constexpr int kExitOk = 0;
    static constexpr int kExitRenderError = 1;
    static constexpr int kExitBadArguments = 2;

private:
    struct Event
    {
        juce::int64 sample = 0;
        juce::MidiMessage message;
    };

    struct Target
    {
        MidiSourceProcessor* midi = nullptr;
        int outChannel = 1;
    };

    void timerCallback() override;
    bool parseArguments (juce::String& error);
    bool readMidiFile (juce::String& error);
    void instancesLoaded();
    void buildRoutingTable();
    void render();         // render thread
    void renderFinished(); // message thread
    void route (const Event& event, int samplePosition);
    void consume (const juce::AudioBuffer<float>& buffer, int numSamples);
    void fail (int exitCode, const juce::String& error);
    void finish (int exitCode);

    const juce::StringArray args;

    // Options.
    juce::File projectFile, midiFile, outFile;
    double sampleRate = 44100.0;
    int blockSize = 512;
    double warmupSeconds = 2.0;
    double tailMaxSeconds = 60.0;

    // Engine (same objects as the app, without device, ports or persistence).
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<PluginManager> plugins;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<InstanceGraph> graph;
    std::unique_ptr<MidiRouter> router;
    std::unique_ptr<ProjectController> controller;

    // MIDI.
    std::vector<Event> events;
    int midiFileType = 0;
    juce::int64 lastEventSample = 0;
    std::vector<Target> routing[17]; // by source channel 1..16
    std::map<MidiSourceProcessor*, std::array<std::bitset<128>, 16>> held;
    juce::int64 messagesRouted = 0, messagesUnrouted = 0, deliveries = 0;

    // Output.
    std::unique_ptr<juce::TemporaryFile> tempFile;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    int latencySamples = 0;
    juce::int64 latencyToSkip = 0;
    juce::int64 outCount = 0;      // output samples produced (after the latency)
    juce::int64 framesWritten = 0;
    juce::int64 lastAudible = -1;  // last output sample above the threshold
    juce::AudioBuffer<float> tail; // output from lastEventSample on
    juce::int64 tailSamples = 0;
    float peak = 0.0f;
    bool writeFailed = false;
    bool tailCut = false;

    // State.
    double startMs = 0.0, loadStartMs = 0.0, renderStartMs = 0.0, renderEndMs = 0.0;
    juce::StringArray warnings;
    bool loading = false;
    bool finished = false;
    std::atomic<bool> renderDone { false };
    juce::String renderError;

    class RenderThread;
    std::unique_ptr<RenderThread> thread;

    JUCE_DECLARE_WEAK_REFERENCEABLE (OfflineRender)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OfflineRender)
};

} // namespace vstplayer
