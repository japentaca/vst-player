#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

namespace vstplayer
{

class AppSettings;
class GainProcessor;
class ToneGeneratorProcessor;

// Owns the audio device and the processing graph.
// Device: WASAPI ("Windows Audio"), 48 kHz / 256 samples by default; the device setup is
// persisted in settings.xml (AudioDeviceManager::createStateXml) and restored on start.
// Graph: AudioProcessorPlayer -> AudioProcessorGraph. Every source (plugin instances via their
// gain node, and until stage 6 the test tone) feeds the stereo output mixer (a GainProcessor at
// 0 dB with a peak meter), which feeds the audio output node. The player re-prepares the graph whenever the device restarts,
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

    double getSampleRate() const;
    int getBufferSize() const;
    juce::String getDeviceTypeName() const;
    juce::String getDeviceName() const;
    // Output latency in samples as reported by the device (0 if closed).
    int getOutputLatencySamples() const;
    // One-line human readable summary (Spanish, for the UI).
    juce::String describeDevice() const;

    void setTestToneEnabled (bool shouldBeEnabled);
    bool isTestToneEnabled() const;
    ToneGeneratorProcessor* getToneGenerator() const;

    // Output mixer: connect stereo sources to its channels 0 and 1.
    juce::AudioProcessorGraph::NodeID getMixerNodeId() const noexcept { return mixerNode->nodeID; }
    GainProcessor* getMixer() const;

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

    juce::AudioProcessorGraph::Node::Ptr outputNode;
    juce::AudioProcessorGraph::Node::Ptr mixerNode;
    juce::AudioProcessorGraph::Node::Ptr toneNode;

    bool callbackAttached = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace vstplayer
