#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cmath>

namespace vstplayer
{

// Stereo 440 Hz sine generator used to validate the audio path without plugins.
// Temporary: removed in stage 6. Enabling/disabling is click-free (short gain ramp).
class ToneGeneratorProcessor final : public juce::AudioProcessor
{
public:
    static constexpr double kFrequencyHz = 440.0;
    static constexpr float kDefaultLevel = 0.2f; // about -14 dBFS

    ToneGeneratorProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    // Thread-safe controls (called from the message thread, read on the audio thread).
    void setEnabled (bool shouldBeEnabled) noexcept { enabled.store (shouldBeEnabled); }
    bool isEnabled() const noexcept { return enabled.load(); }
    void setLevel (float linearGain) noexcept { level.store (linearGain); }

    // Diagnostics, readable from any thread.
    double getPreparedSampleRate() const noexcept { return preparedSampleRate.load(); }
    int getPreparedBlockSize() const noexcept { return preparedBlockSize.load(); }
    int getPrepareCount() const noexcept { return prepareCount.load(); }
    juce::int64 getBlocksProcessed() const noexcept { return blocksProcessed.load(); }
    int getLastBlockSize() const noexcept { return lastBlockSize.load(); }
    float getLastPeak() const noexcept { return lastPeak.load(); }

    //==============================================================================
    const juce::String getName() const override { return "Tono de prueba"; }

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override
    {
        phaseDelta = juce::MathConstants<double>::twoPi * kFrequencyHz / sampleRate;
        gain.reset (sampleRate, 0.02);
        gain.setCurrentAndTargetValue (enabled.load() ? level.load() : 0.0f);

        preparedSampleRate.store (sampleRate);
        preparedBlockSize.store (maximumExpectedSamplesPerBlock);
        prepareCount.fetch_add (1);
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto& out = layouts.getMainOutputChannelSet();
        return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        const auto numSamples = buffer.getNumSamples();
        const auto numChannels = buffer.getNumChannels();
        buffer.clear();

        gain.setTargetValue (enabled.load() ? level.load() : 0.0f);

        float peak = 0.0f;

        if (gain.isSmoothing() || gain.getCurrentValue() > 0.0f)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const auto sample = (float) std::sin (phase) * gain.getNextValue();
                phase += phaseDelta;

                if (phase >= juce::MathConstants<double>::twoPi)
                    phase -= juce::MathConstants<double>::twoPi;

                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.setSample (ch, i, sample);

                peak = juce::jmax (peak, std::abs (sample));
            }
        }

        lastPeak.store (peak);
        lastBlockSize.store (numSamples);
        blocksProcessed.fetch_add (1);
    }

    using AudioProcessor::processBlock;

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    std::atomic<bool> enabled { false };
    std::atomic<float> level { kDefaultLevel };

    double phase = 0.0;
    double phaseDelta = 0.0;
    juce::LinearSmoothedValue<float> gain;

    std::atomic<double> preparedSampleRate { 0.0 };
    std::atomic<int> preparedBlockSize { 0 };
    std::atomic<int> prepareCount { 0 };
    std::atomic<juce::int64> blocksProcessed { 0 };
    std::atomic<int> lastBlockSize { 0 };
    std::atomic<float> lastPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneGeneratorProcessor)
};

} // namespace vstplayer
