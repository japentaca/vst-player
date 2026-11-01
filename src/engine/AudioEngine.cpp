#include "engine/AudioEngine.h"

#include "app/AppSettings.h"
#include "engine/GainProcessor.h"
#include "engine/ToneGenerator.h"

namespace vstplayer
{

namespace
{
constexpr const char* kAudioStateKey = "audioDeviceState";
constexpr int kNumInputChannels = 0;
constexpr int kNumOutputChannels = 2;

using IOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;
} // namespace

AudioEngine::AudioEngine (AppSettings& s, bool persist)
    : settings (s), persistState (persist)
{
    buildGraph();
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeChangeListener (this);

    if (callbackAttached)
        deviceManager.removeAudioCallback (&player);

    player.setProcessor (nullptr);
    deviceManager.closeAudioDevice();
    graph.clear();
}

void AudioEngine::buildGraph()
{
    graph.setPlayConfigDetails (kNumInputChannels, kNumOutputChannels, kDefaultSampleRate, kDefaultBufferSize);

    outputNode = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioOutputNode));
    mixerNode = graph.addNode (std::make_unique<GainProcessor> ("Mezcla"));
    toneNode = graph.addNode (std::make_unique<ToneGeneratorProcessor>());

    for (int ch = 0; ch < kNumOutputChannels; ++ch)
    {
        graph.addConnection ({ { toneNode->nodeID, ch }, { mixerNode->nodeID, ch } });
        graph.addConnection ({ { mixerNode->nodeID, ch }, { outputNode->nodeID, ch } });
    }
}

juce::String AudioEngine::initialise()
{
    const auto savedState = settings.getProperties().getXmlValue (kAudioStateKey);

    juce::AudioDeviceManager::AudioDeviceSetup preferred;
    preferred.sampleRate = kDefaultSampleRate;
    preferred.bufferSize = kDefaultBufferSize;

    auto error = deviceManager.initialise (kNumInputChannels, kNumOutputChannels, savedState.get(),
                                           true, {}, &preferred);

    // First run (nothing saved): make sure we are on WASAPI shared mode with our defaults, and
    // set them explicitly so createStateXml() has something to persist. The device may round
    // the request (WASAPI shared mode only offers the mixer period, often 480 samples).
    if (savedState == nullptr)
    {
        if (deviceManager.getCurrentAudioDeviceType() != kPreferredDeviceType)
            deviceManager.setCurrentAudioDeviceType (kPreferredDeviceType, true);

        auto setup = deviceManager.getAudioDeviceSetup();
        setup.sampleRate = kDefaultSampleRate;
        setup.bufferSize = kDefaultBufferSize;
        error = deviceManager.setAudioDeviceSetup (setup, true);
    }

    player.setProcessor (&graph);
    deviceManager.addAudioCallback (&player);
    callbackAttached = true;

    deviceManager.addChangeListener (this);
    saveDeviceState();

    if (error.isEmpty() && deviceManager.getCurrentAudioDevice() == nullptr)
        error = juce::String::fromUTF8 ("No hay ningún dispositivo de audio disponible.");

    return error;
}

double AudioEngine::getSampleRate() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getCurrentSampleRate();

    return 0.0;
}

int AudioEngine::getBufferSize() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getCurrentBufferSizeSamples();

    return 0;
}

juce::String AudioEngine::getDeviceTypeName() const
{
    return deviceManager.getCurrentAudioDeviceType();
}

juce::String AudioEngine::getDeviceName() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getName();

    return {};
}

int AudioEngine::getOutputLatencySamples() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getOutputLatencyInSamples();

    return 0;
}

juce::String AudioEngine::describeDevice() const
{
    if (deviceManager.getCurrentAudioDevice() == nullptr)
        return juce::String::fromUTF8 ("Audio: sin dispositivo abierto");

    const auto sampleRate = getSampleRate();
    const auto latencyMs = sampleRate > 0.0 ? 1000.0 * getOutputLatencySamples() / sampleRate : 0.0;

    return "Audio: " + getDeviceTypeName() + " / " + getDeviceName()
           + " - " + juce::String (sampleRate, 0) + " Hz, buffer " + juce::String (getBufferSize())
           + " - latencia de salida " + juce::String (latencyMs, 1) + " ms";
}

void AudioEngine::setTestToneEnabled (bool shouldBeEnabled)
{
    if (auto* tone = getToneGenerator())
        tone->setEnabled (shouldBeEnabled);
}

bool AudioEngine::isTestToneEnabled() const
{
    if (auto* tone = getToneGenerator())
        return tone->isEnabled();

    return false;
}

ToneGeneratorProcessor* AudioEngine::getToneGenerator() const
{
    return toneNode != nullptr ? dynamic_cast<ToneGeneratorProcessor*> (toneNode->getProcessor()) : nullptr;
}

GainProcessor* AudioEngine::getMixer() const
{
    return mixerNode != nullptr ? dynamic_cast<GainProcessor*> (mixerNode->getProcessor()) : nullptr;
}

void AudioEngine::saveDeviceState()
{
    if (! persistState)
        return;

    if (auto state = deviceManager.createStateXml())
        settings.getProperties().setValue (kAudioStateKey, state.get());
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    saveDeviceState();
}

} // namespace vstplayer
