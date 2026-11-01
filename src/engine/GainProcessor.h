#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cmath>

namespace vstplayer
{

// Stereo gain stage with mute and a peak meter. Used after every instance (per-instance
// gain) and as the output mixer (several gain nodes connected to its input: the graph sums
// them). Gain changes are smoothed (20 ms) so moving the slider or muting does not click.
// Controls and meters are atomics: set from the message thread, read on the audio thread.
class GainProcessor final : public juce::AudioProcessor
{
public:
    static constexpr float kMinGainDb = -60.0f; // at or below: silence
    static constexpr float kMaxGainDb = 12.0f;

    explicit GainProcessor (juce::String nameToUse = "Ganancia")
        : AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          name (std::move (nameToUse))
    {
    }

    void setGainDb (float newGainDb) noexcept { gainDb.store (juce::jlimit (kMinGainDb, kMaxGainDb, newGainDb)); }
    float getGainDb() const noexcept { return gainDb.load(); }
    void setMute (bool shouldMute) noexcept { muted.store (shouldMute); }
    bool isMuted() const noexcept { return muted.load(); }

    // Highest absolute output sample (any channel) since the last call; resets the hold.
    float getAndResetPeak() noexcept { return peakHold.exchange (0.0f); }
    // Per channel output peak of the last processed block (0 = left, 1 = right).
    float getLastBlockPeak (int channel) const noexcept { return lastBlockPeak[channel == 0 ? 0 : 1].load(); }

    // Level meter of the UI: highest absolute output sample of one channel since the last
    // call (0 = left, 1 = right); resets that channel's hold. Independent of getAndResetPeak.
    float getAndResetMeterPeak (int channel) noexcept { return meterPeak[channel == 0 ? 0 : 1].exchange (0.0f); }

    // Diagnostics (tests): last prepareToPlay arguments and processed block count.
    double getPreparedSampleRate() const noexcept { return preparedSampleRate.load(); }
    int getPreparedBlockSize() const noexcept { return preparedBlockSize.load(); }
    int getPrepareCount() const noexcept { return prepareCount.load(); }
    int getLastBlockSize() const noexcept { return lastBlockSize.load(); }
    juce::int64 getBlocksProcessed() const noexcept { return blocksProcessed.load(); }

    //==============================================================================
    const juce::String getName() const override { return name; }

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override
    {
        smoothedGain.reset (sampleRate, 0.02);
        smoothedGain.setCurrentAndTargetValue (targetLinearGain());
        preparedSampleRate.store (sampleRate);
        preparedBlockSize.store (maximumExpectedSamplesPerBlock);
        prepareCount.fetch_add (1);
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
               && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        const auto numSamples = buffer.getNumSamples();
        const auto numChannels = juce::jmin (2, buffer.getNumChannels());

        smoothedGain.setTargetValue (targetLinearGain());

        if (smoothedGain.isSmoothing())
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const auto g = smoothedGain.getNextValue();

                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
            }
        }
        else
        {
            const auto g = smoothedGain.getCurrentValue();

            if (g == 0.0f)
                buffer.clear();
            else if (g != 1.0f)
                buffer.applyGain (g);
        }

        float blockPeak = 0.0f;

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto p = ch < numChannels ? buffer.getMagnitude (ch, 0, numSamples) : 0.0f;
            lastBlockPeak[ch].store (p);
            blockPeak = juce::jmax (blockPeak, p);

            auto previousMeter = meterPeak[ch].load();
            while (p > previousMeter && ! meterPeak[ch].compare_exchange_weak (previousMeter, p)) {}
        }

        auto previous = peakHold.load();
        while (blockPeak > previous && ! peakHold.compare_exchange_weak (previous, blockPeak)) {}

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
    float targetLinearGain() const noexcept
    {
        const auto db = gainDb.load();
        return (muted.load() || db <= kMinGainDb) ? 0.0f : juce::Decibels::decibelsToGain (db);
    }

    const juce::String name;

    std::atomic<float> gainDb { 0.0f };
    std::atomic<bool> muted { false };
    juce::LinearSmoothedValue<float> smoothedGain;

    std::atomic<float> peakHold { 0.0f };
    std::atomic<float> lastBlockPeak[2] { 0.0f, 0.0f };
    std::atomic<float> meterPeak[2] { 0.0f, 0.0f };
    std::atomic<int> prepareCount { 0 };
    std::atomic<int> lastBlockSize { 0 };
    std::atomic<double> preparedSampleRate { 0.0 };
    std::atomic<int> preparedBlockSize { 0 };
    std::atomic<juce::int64> blocksProcessed { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainProcessor)
};

} // namespace vstplayer
