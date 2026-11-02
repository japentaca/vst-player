#pragma once

#include "engine/Mp3Writer.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>

namespace vstplayer
{

// Records what the audio device plays (the output mixer, channels 0 and 1) to disk, in real time.
//
// - Audio thread: processBlock() copies every device block into a ThreadedWriter FIFO (10 s,
//   lock-free; a block that does not fit is dropped and counted). Nothing is encoded there.
// - Writer thread (TimeSliceThread): drains the FIFO into the MP3 encoder (Mp3Writer, LAME) and,
//   optionally, a 32-bit float WAV. The MP3 is encoded while recording, so stop() only flushes
//   what is left (a few ms): no conversion afterwards. The WAV header is rewritten every 5 s, so
//   a crash leaves a readable WAV.
// - Message thread: start() / stop() and the listeners. A device stop (device or sample rate
//   change, device lost) or a write error (disk full) stops the recording asynchronously
//   (reason device / error): a file never mixes two sample rates.
//
// Files: "<folder>\<base> <yyyy-mm-dd HH-MM-SS>.mp3" (and .wav), " (2)" etc. if the name exists.
class Recorder final : private juce::AsyncUpdater
{
public:
    enum class StopReason
    {
        user,     // UI button / shortcut
        api,      // API command
        device,   // the audio device stopped or changed
        error,    // write error
        shutdown  // app closing
    };

    struct Options
    {
        juce::File folder;
        juce::String baseName;  // project name ("Sin título" when empty); made a legal file name
        RecordingFormat format;
        bool keepWav = false;   // also a WAV next to the MP3 (always for format "wav")
        juce::String title;     // ID3 title (default: baseName)
    };

    struct Info
    {
        bool active = false;
        juce::File mp3File, wavFile; // juce::File() when not written
        RecordingFormat format;
        double sampleRate = 0.0;
        double seconds = 0.0;
        juce::int64 droppedSamples = 0;
        float peak = 0.0f;           // highest absolute sample (> 1 = clipped in the MP3)
        juce::Time startTime;
    };

    struct Result
    {
        bool ok = false;
        juce::String error;
        StopReason reason = StopReason::user;
        Info info;                   // final values (active = false)
        juce::int64 mp3Bytes = 0, wavBytes = 0;
    };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void recordingStarted (const Info&) {}
        virtual void recordingStopped (const Result&) {}
    };

    Recorder();
    ~Recorder() override;

    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // Message thread. sampleRate = the device's (0 = no device: error).
    juce::Result start (const Options& options, double sampleRate);
    // Message thread. Result.ok = false with an error if nothing was recording.
    Result stop (StopReason reason);
    // Stops without notifying the listeners (AudioEngine destructor).
    void stopSilently();

    bool isRecording() const noexcept { return recording; }
    Info getInfo() const;

    // Audio thread (AudioEngine's device callback).
    void processBlock (const float* const* outputs, int numOutputs, int numSamples) noexcept;
    // Device callback stopped (any thread).
    void audioDeviceStopped() noexcept;

    // "<project> 2026-11-02 21-35-07" (legal file name; "Sin título" for an empty name).
    static juce::String makeBaseName (const juce::String& projectName, juce::Time time);
    static juce::String reasonKey (StopReason reason); // "user", "api", "device", "error", "shutdown"

    static constexpr double kFifoSeconds = 10.0;
    static constexpr double kWavFlushSeconds = 5.0;

private:
    class SplitWriter;

    void handleAsyncUpdate() override;
    Result finishRecording (StopReason reason);

    juce::TimeSliceThread writerThread { "VstPlayer recorder" };
    juce::SpinLock writerLock; // audio thread (try-lock) vs start / stop
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threaded; // guarded by writerLock
    SplitWriter* split = nullptr;                                       // owned by `threaded`

    std::atomic<bool> active { false };        // audio thread may write
    std::atomic<bool> deviceStopped { false };
    std::atomic<bool> writeFailed { false };
    std::atomic<juce::int64> samplesWritten { 0 }, samplesDropped { 0 };
    std::atomic<float> peak { 0.0f };

    // Message thread.
    bool recording = false;
    Info current;
    juce::ListenerList<Listener> listeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Recorder)
};

} // namespace vstplayer
