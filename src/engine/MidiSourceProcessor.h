#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace vstplayer
{

// Per-instance MIDI entry point: a graph node with no audio whose MIDI output is connected to
// the plugin node. Any thread can queue messages (virtual keyboard now, MIDI input ports in
// stage 4) through the MidiMessageCollector, which turns their timestamps into sample offsets
// inside the next audio block.
//
// Panic is a flag consumed on the audio thread: pending messages are dropped and the block
// starts with sustain off + all notes off + all sound off on the 16 channels.
class MidiSourceProcessor final : public juce::AudioProcessor
{
public:
    MidiSourceProcessor()
        : AudioProcessor (BusesProperties())
    {
        // The collector asserts if used before reset(); the real rate arrives in prepareToPlay.
        collector.reset (48000.0);
    }

    // Thread-safe. The message timestamp must be Time::getMillisecondCounterHiRes() * 0.001
    // (as MidiInput does); a zero timestamp is replaced by "now".
    void addMessage (juce::MidiMessage message)
    {
        if (message.getTimeStamp() <= 0.0)
            message.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);

        collector.addMessageToQueue (message);
        messagesQueued.fetch_add (1);
    }

    void requestPanic() noexcept { panicRequested.store (true); }

    // Diagnostics (tests, MIDI activity LED in later stages).
    juce::int64 getMessagesQueued() const noexcept { return messagesQueued.load(); }
    juce::int64 getEventsDelivered() const noexcept { return eventsDelivered.load(); }

    //==============================================================================
    const juce::String getName() const override { return "Entrada MIDI"; }

    void prepareToPlay (double sampleRate, int) override { collector.reset (sampleRate); }
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        midi.clear();
        const auto numSamples = juce::jmax (1, buffer.getNumSamples());

        if (panicRequested.exchange (false))
        {
            collector.removeNextBlockOfMessages (midi, numSamples); // drop what was pending
            midi.clear();

            for (int channel = 1; channel <= 16; ++channel)
            {
                midi.addEvent (juce::MidiMessage::controllerEvent (channel, 64, 0), 0);
                midi.addEvent (juce::MidiMessage::allNotesOff (channel), 0);
                midi.addEvent (juce::MidiMessage::allSoundOff (channel), 0);
            }
        }
        else
        {
            collector.removeNextBlockOfMessages (midi, numSamples);
        }

        eventsDelivered.fetch_add (midi.getNumEvents());
    }

    using AudioProcessor::processBlock;

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }

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
    juce::MidiMessageCollector collector;
    std::atomic<bool> panicRequested { false };
    std::atomic<juce::int64> messagesQueued { 0 };
    std::atomic<juce::int64> eventsDelivered { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiSourceProcessor)
};

} // namespace vstplayer
