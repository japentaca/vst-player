#include "engine/AudioEngine.h"

#include "app/AppSettings.h"
#include "engine/GainProcessor.h"

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
    recorder.stopSilently();
    deviceManager.removeChangeListener (this);

    if (callbackAttached)
        deviceManager.removeAudioCallback (&timedCallback);

    player.setProcessor (nullptr);
    deviceManager.closeAudioDevice();
    graph.clear();
}

void AudioEngine::buildGraph()
{
    graph.setPlayConfigDetails (kNumInputChannels, kNumOutputChannels, kDefaultSampleRate, kDefaultBufferSize);

    outputNode = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioOutputNode));
    mixerNode = graph.addNode (std::make_unique<GainProcessor> ("Mezcla"));

    for (int ch = 0; ch < kNumOutputChannels; ++ch)
        graph.addConnection ({ { mixerNode->nodeID, ch }, { outputNode->nodeID, ch } });
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
    deviceManager.addAudioCallback (&timedCallback);
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

    return offlineSampleRate;
}

int AudioEngine::getBufferSize() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getCurrentBufferSizeSamples();

    return offlineBufferSize;
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

juce::StringArray AudioEngine::applyDeviceSetup (const juce::String& typeName, const juce::String& outputDevice,
                                                 double sampleRate, int bufferSize)
{
    juce::StringArray problems;

    auto targetType = deviceManager.getCurrentAudioDeviceType();
    juce::AudioIODeviceType* type = deviceManager.getCurrentDeviceTypeObject();

    if (typeName.isNotEmpty() && typeName != targetType)
    {
        juce::AudioIODeviceType* found = nullptr;

        for (auto* t : deviceManager.getAvailableDeviceTypes())
            if (t->getTypeName() == typeName)
                found = t;

        if (found != nullptr)
        {
            targetType = typeName;
            type = found;
        }
        else
        {
            problems.add (juce::String::fromUTF8 ("El tipo de audio \"") + typeName
                          + juce::String::fromUTF8 ("\" no está disponible; se usa \"") + targetType + "\".");
        }
    }

    juce::String targetDevice;

    if (outputDevice.isNotEmpty() && type != nullptr)
    {
        type->scanForDevices();

        if (type->getDeviceNames (false).contains (outputDevice))
            targetDevice = outputDevice;
        else
            problems.add (juce::String::fromUTF8 ("El dispositivo de salida \"") + outputDevice
                          + juce::String::fromUTF8 ("\" no está conectado; se usa el actual o el predeterminado."));
    }

    const auto typeDiffers = targetType != deviceManager.getCurrentAudioDeviceType();
    const auto deviceDiffers = targetDevice.isNotEmpty() && targetDevice != getDeviceName();
    const auto rateDiffers = sampleRate > 0.0 && std::abs (sampleRate - getSampleRate()) > 0.5;
    const auto bufferDiffers = bufferSize > 0 && bufferSize != getBufferSize();

    if (! (typeDiffers || deviceDiffers || rateDiffers || bufferDiffers || deviceManager.getCurrentAudioDevice() == nullptr))
        return problems;

    if (typeDiffers)
        deviceManager.setCurrentAudioDeviceType (targetType, true);

    auto setup = deviceManager.getAudioDeviceSetup();

    if (targetDevice.isNotEmpty())
        setup.outputDeviceName = targetDevice;

    if (sampleRate > 0.0)
        setup.sampleRate = sampleRate;

    if (bufferSize > 0)
        setup.bufferSize = bufferSize;

    setup.useDefaultOutputChannels = true;

    if (const auto error = deviceManager.setAudioDeviceSetup (setup, true); error.isNotEmpty())
        problems.add ("Audio: " + error);

    if (deviceManager.getCurrentAudioDevice() == nullptr)
    {
        problems.add (juce::String::fromUTF8 ("No se pudo abrir el dispositivo de audio del proyecto."));
    }
    else
    {
        if (sampleRate > 0.0 && std::abs (sampleRate - getSampleRate()) > 0.5)
            problems.add ("El dispositivo usa " + juce::String (getSampleRate(), 0) + " Hz en lugar de " + juce::String (sampleRate, 0) + " Hz.");

        if (bufferSize > 0 && bufferSize != getBufferSize())
            problems.add ("El dispositivo usa un buffer de " + juce::String (getBufferSize()) + " en lugar de " + juce::String (bufferSize) + ".");
    }

    return problems;
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

namespace vstplayer
{

//==============================================================================
void AudioEngine::TimedCallback::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device != nullptr && device->getCurrentSampleRate() > 0.0)
        sampleRate.store (device->getCurrentSampleRate());

    target.audioDeviceAboutToStart (device);
}

void AudioEngine::TimedCallback::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                                   int numSamples, const juce::AudioIODeviceCallbackContext& context)
{
    const auto start = juce::Time::getHighResolutionTicks();
    target.audioDeviceIOCallbackWithContext (in, numIn, out, numOut, numSamples, context);
    recorder.processBlock (out, numOut, numSamples);
    const auto ms = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start) * 1000.0;

    const auto budget = 1000.0 * numSamples / sampleRate.load();
    budgetMs.store (budget);
    count.fetch_add (1);
    total.fetch_add (1);
    sumMs.store (sumMs.load() + ms); // single writer (the audio thread)

    if (ms > maxMs.load())
        maxMs.store (ms);

    if (ms > budget)
        overBudget.fetch_add (1);
}

AudioEngine::CallbackStats AudioEngine::getAndResetCallbackStats()
{
    CallbackStats s;
    s.callbacks = timedCallback.count.exchange (0);
    const auto sum = timedCallback.sumMs.exchange (0.0);
    s.averageMs = s.callbacks > 0 ? sum / (double) s.callbacks : 0.0;
    s.maxMs = timedCallback.maxMs.exchange (0.0);
    s.budgetMs = timedCallback.budgetMs.load();
    s.overBudget = timedCallback.overBudget.exchange (0);
    s.totalCallbacks = timedCallback.total.load();
    return s;
}

} // namespace vstplayer
