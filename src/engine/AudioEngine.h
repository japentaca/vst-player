#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>

namespace vstplayer
{

class AppSettings;
class GainProcessor;

// Owns the audio device and the processing graph.
// Device: WASAPI ("Windows Audio"), 48 kHz / 256 samples by default; the device setup is
// persisted in settings.xml (AudioDeviceManager::createStateXml) and restored on start.
// Graph: AudioProcessorPlayer -> AudioProcessorGraph. Every plugin instance (through its gain
// node) feeds the stereo output mixer (a GainProcessor at 0 dB with a peak meter), which feeds
// the audio output node. (The stage 2 test tone was removed in stage 6.) The player re-prepares the graph whenever the device restarts,
// so sample rate / buffer size changes reach every node through prepareToPlay.
class AudioEngine final : private juce::ChangeListener
{
public:
    // persistState = false: never writes the device setup back to settings (used by self-tests).
    explicit AudioEngine (AppSettings& settings, bool persistState = true);
    ~AudioEngine() override;

    // Opens the audio device. Returns an empty string on success, or the error message.
    juce::String initialise();

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    juce::AudioProcessorGraph& getGraph() noexcept { return graph; }
    juce::AudioProcessorPlayer& getPlayer() noexcept { return player; }

    // The device's, or (no device open) the offline format, 0 if none was set.
    double getSampleRate() const;
    int getBufferSize() const;
    // Offline render: the format plugins are created with while no device is open.
    void setOfflineFormat (double sampleRate, int bufferSize) noexcept
    {
        offlineSampleRate = sampleRate;
        offlineBufferSize = bufferSize;
    }
    juce::String getDeviceTypeName() const;
    juce::String getDeviceName() const;
    // Output latency in samples as reported by the device (0 if closed).
    int getOutputLatencySamples() const;
    // One-line human readable summary (Spanish, for the UI).
    juce::String describeDevice() const;

    // Audio callback load, 0..1 (AudioDeviceManager::getCpuUsage).
    double getCpuUsage() const { return deviceManager.getCpuUsage(); }

    // Output mixer: connect stereo sources to its channels 0 and 1.
    juce::AudioProcessorGraph::NodeID getMixerNodeId() const noexcept { return mixerNode->nodeID; }
    GainProcessor* getMixer() const;

    // Switches to the given device type / output device / sample rate / buffer size (a project's
    // "audio" block). Empty or zero values keep the current setting. Nothing is reopened when
    // the device already matches. Returns the problems found (type or device not available,
    // device that rounded the request...), one per line; empty = applied exactly.
    juce::StringArray applyDeviceSetup (const juce::String& typeName, const juce::String& outputDevice, double sampleRate,
                                        int bufferSize);

    // Audio callback timing since the last call (then reset). Any thread.
    struct CallbackStats
    {
        juce::int64 callbacks = 0;     // device callbacks measured
        double averageMs = 0.0;        // mean time spent in the callback (whole graph)
        double maxMs = 0.0;            // slowest callback
        double budgetMs = 0.0;         // block duration (buffer / sample rate) of the last callback
        juce::int64 overBudget = 0;    // callbacks that took longer than their block duration
        juce::int64 totalCallbacks = 0; // since the device started (not reset)
    };
    CallbackStats getAndResetCallbackStats();
    // Buffer underruns / overruns reported by the device (-1 if the driver does not count them).
    int getXRunCount() const { return deviceManager.getXRunCount(); }

    // Writes the current device setup into settings (no-op when persistState is false).
    void saveDeviceState();

    static constexpr const char* kPreferredDeviceType = "Windows Audio";
    static constexpr double kDefaultSampleRate = 48000.0;
    static constexpr int kDefaultBufferSize = 256;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void buildGraph();

    AppSettings& settings;
    const bool persistState;

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorPlayer player;

    // Forwards the device callback to the player and measures how long it takes.
    class TimedCallback final : public juce::AudioIODeviceCallback
    {
    public:
        explicit TimedCallback (juce::AudioIODeviceCallback& t) : target (t) {}

        void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int numSamples,
                                               const juce::AudioIODeviceCallbackContext& context) override;
        void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
        void audioDeviceStopped() override { target.audioDeviceStopped(); }
        void audioDeviceError (const juce::String& message) override { target.audioDeviceError (message); }

        juce::AudioIODeviceCallback& target;
        std::atomic<double> sampleRate { 48000.0 };
        std::atomic<juce::int64> count { 0 }, total { 0 }, overBudget { 0 };
        std::atomic<double> sumMs { 0.0 }, maxMs { 0.0 }, budgetMs { 0.0 };
    };

    TimedCallback timedCallback { player };

    juce::AudioProcessorGraph::Node::Ptr outputNode;
    juce::AudioProcessorGraph::Node::Ptr mixerNode;

    bool callbackAttached = false;
    double offlineSampleRate = 0.0;
    int offlineBufferSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace vstplayer
