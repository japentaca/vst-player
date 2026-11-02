#pragma once

#include "engine/MidiMixControls.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>

namespace vstplayer
{

// Per-instance MIDI entry point: a graph node with no audio whose MIDI output is connected to
// the plugin node. Any thread can queue messages (virtual keyboard, MidiRouter for the MIDI input
// ports) through the MidiMessageCollector, which turns their timestamps into sample offsets
// inside the next audio block.
//
// Panic is a flag consumed on the audio thread: pending messages are dropped and the block
// starts with sustain off + all notes off + all sound off on the 16 channels.
//
// Volume (CC7) and pan (CC10), on any channel, are taken out of the stream and stored in the
// instance's MidiMixControls (setMidiMix), which its GainProcessor applies: the same mix for
// every plugin, and no double attenuation in the ones that also honour those controllers.
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

    // Offline render only: a message for the next processBlock at an exact sample position
    // (0 .. block size - 1). Not thread-safe: call it from the thread that runs the graph,
    // between two blocks. Merged after the collector's messages (same position: these go
    // last) and before CC7 / CC10 are taken out, so the mix controllers work as live.
    void addOfflineMessage (const juce::MidiMessage& message, int samplePosition)
    {
        offlineMessages.addEvent (message, juce::jmax (0, samplePosition));
        messagesQueued.fetch_add (1);
    }

    // Not thread-safe: only before the processor is in the graph.
    void setMidiMix (std::shared_ptr<MidiMixControls> controls) { midiMix = std::move (controls); }

    // Diagnostics (tests, MIDI activity LED in later stages).
    juce::int64 getMessagesQueued() const noexcept { return messagesQueued.load(); }
    juce::int64 getEventsDelivered() const noexcept { return eventsDelivered.load(); }
    // CC7 / CC10 kept for the gain node instead of reaching the plugin.
    juce::int64 getMixControllersTaken() const noexcept { return mixControllersTaken.load(); }
    // Note-ons / note-offs handed to the plugin, by channel 1..16 (routing tests).
    int getNoteOnsDelivered (int channel) const noexcept { return channel >= 1 && channel <= 16 ? noteOnsDelivered[(size_t) channel - 1].load() : 0; }
    int getNoteOffsDelivered (int channel) const noexcept { return channel >= 1 && channel <= 16 ? noteOffsDelivered[(size_t) channel - 1].load() : 0; }

    //==============================================================================
    const juce::String getName() const override { return "Entrada MIDI"; }

    void prepareToPlay (double sampleRate, int) override
    {
        collector.reset (sampleRate);
        filtered.ensureSize (4096); // no allocation on the audio thread for normal blocks
        offlineMessages.ensureSize (4096);
    }
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        midi.clear();
        const auto numSamples = juce::jmax (1, buffer.getNumSamples());

        if (panicRequested.exchange (false))
        {
            collector.removeNextBlockOfMessages (midi, numSamples); // drop what was pending
            offlineMessages.clear();
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

            if (! offlineMessages.isEmpty())
            {
                midi.addEvents (offlineMessages, 0, numSamples, 0);
                offlineMessages.clear();
            }

            takeMixControllers (midi);
        }

        eventsDelivered.fetch_add (midi.getNumEvents());

        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            const auto index = (size_t) (message.getChannel() - 1);

            if (index < 16)
            {
                if (message.isNoteOn())
                    noteOnsDelivered[index].fetch_add (1);
                else if (message.isNoteOff())
                    noteOffsDelivered[index].fetch_add (1);
            }
        }
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
    static bool isMixController (const juce::MidiMessage& m) noexcept
    {
        return m.isController() && (m.getControllerNumber() == 7 || m.getControllerNumber() == 10);
    }

    // Stores CC7 / CC10 in midiMix (the last one of the block wins) and removes them.
    void takeMixControllers (juce::MidiBuffer& midi)
    {
        if (midiMix == nullptr)
            return;

        bool found = false;

        for (const auto metadata : midi)
        {
            if (isMixController (metadata.getMessage()))
            {
                found = true;
                break;
            }
        }

        if (! found)
            return;

        filtered.clear();

        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();

            if (isMixController (message))
            {
                auto& target = message.getControllerNumber() == 7 ? midiMix->volumeCc : midiMix->panCc;
                target.store (message.getControllerValue());
                mixControllersTaken.fetch_add (1);
            }
            else
            {
                filtered.addEvent (message, metadata.samplePosition);
            }
        }

        midi.swapWith (filtered);
    }

    juce::MidiMessageCollector collector;
    std::shared_ptr<MidiMixControls> midiMix;
    juce::MidiBuffer filtered;
    juce::MidiBuffer offlineMessages; // addOfflineMessage, consumed by the next block
    std::atomic<juce::int64> mixControllersTaken { 0 };
    std::atomic<bool> panicRequested { false };
    std::atomic<juce::int64> messagesQueued { 0 };
    std::atomic<juce::int64> eventsDelivered { 0 };
    std::array<std::atomic<int>, 16> noteOnsDelivered {};
    std::array<std::atomic<int>, 16> noteOffsDelivered {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiSourceProcessor)
};

} // namespace vstplayer
