#include "engine/Recorder.h"

namespace vstplayer
{

namespace
{
juce::File uniqueSibling (const juce::File& folder, const juce::String& base, bool mp3, bool wav)
{
    for (int n = 1;; ++n)
    {
        const auto name = n == 1 ? base : base + " (" + juce::String (n) + ")";
        const auto mp3File = folder.getChildFile (name + ".mp3");
        const auto wavFile = folder.getChildFile (name + ".wav");

        if ((! mp3 || ! mp3File.exists()) && (! wav || ! wavFile.exists()))
            return folder.getChildFile (name);
    }
}
} // namespace

//==============================================================================
// Writer behind the ThreadedWriter: forwards every block to the MP3 encoder and / or the WAV
// writer (writer thread). Reports write errors to the Recorder.
class Recorder::SplitWriter final : public juce::AudioFormatWriter
{
public:
    SplitWriter (Recorder& r, double rate, std::unique_ptr<Mp3Writer> mp3Writer, std::unique_ptr<juce::AudioFormatWriter> wavWriter)
        : AudioFormatWriter (nullptr, "VstPlayer recording", rate, 2u, 32u),
          recorder (r), mp3 (std::move (mp3Writer)), wav (std::move (wavWriter))
    {
        usesFloatingPointData = true;
    }

    ~SplitWriter() override
    {
        // Finishes the files: MP3 flush + info frame, WAV header.
        mp3 = nullptr;
        wav = nullptr;
    }

    bool write (const int** samples, int numSamples) override
    {
        auto ok = true;

        if (mp3 != nullptr)
            ok = mp3->write (samples, numSamples) && ok;

        if (wav != nullptr)
            ok = wav->write (samples, numSamples) && ok;

        if (! ok)
            reportFailure();

        return ok;
    }

    bool flush() override
    {
        if (wav != nullptr && ! wav->flush())
            return false;

        return true;
    }

private:
    void reportFailure()
    {
        if (! recorder.writeFailed.exchange (true))
            recorder.triggerAsyncUpdate();
    }

    Recorder& recorder;
    std::unique_ptr<Mp3Writer> mp3;
    std::unique_ptr<juce::AudioFormatWriter> wav;
};

//==============================================================================
Recorder::Recorder()
{
    writerThread.startThread();
}

Recorder::~Recorder()
{
    stopSilently();
    cancelPendingUpdate();
    writerThread.stopThread (5000);
}

juce::String Recorder::makeBaseName (const juce::String& projectName, juce::Time time)
{
    auto base = juce::File::createLegalFileName (projectName.trim()).trim();

    if (base.isEmpty())
        base = juce::String::fromUTF8 ("Sin título");

    return base + " " + time.formatted ("%Y-%m-%d %H-%M-%S");
}

juce::String Recorder::reasonKey (StopReason reason)
{
    switch (reason)
    {
        case StopReason::user:     return "user";
        case StopReason::api:      return "api";
        case StopReason::device:   return "device";
        case StopReason::error:    return "error";
        case StopReason::shutdown: return "shutdown";
    }

    return "user";
}

juce::Result Recorder::start (const Options& options, double sampleRate)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (recording)
        return juce::Result::fail (juce::String::fromUTF8 ("Ya se está grabando."));

    if (sampleRate <= 0.0)
        return juce::Result::fail (juce::String::fromUTF8 ("No hay ningún dispositivo de audio abierto."));

    const auto folder = options.folder;

    if (folder == juce::File() || ! folder.createDirectory())
        return juce::Result::fail ("No se pudo crear la carpeta de grabaciones: " + folder.getFullPathName());

    const auto now = juce::Time::getCurrentTime();
    const auto wantMp3 = options.format.isMp3();
    const auto wantWav = options.keepWav || ! wantMp3;
    const auto stem = uniqueSibling (folder, makeBaseName (options.baseName, now), wantMp3, wantWav);
    // Not withFileExtension: it would cut a name with dots ("v1.2 2026-…").
    const auto mp3Path = wantMp3 ? folder.getChildFile (stem.getFileName() + ".mp3") : juce::File();
    const auto wavPath = wantWav ? folder.getChildFile (stem.getFileName() + ".wav") : juce::File();

    const auto cleanUp = [mp3Path, wavPath]
    {
        if (mp3Path != juce::File())
            mp3Path.deleteFile();

        if (wavPath != juce::File())
            wavPath.deleteFile();
    };

    std::unique_ptr<Mp3Writer> mp3;
    std::unique_ptr<juce::AudioFormatWriter> wav;

    if (wantMp3)
    {
        Mp3Writer::Tags tags;
        tags.title = options.title.isNotEmpty() ? options.title
                                                : (options.baseName.trim().isNotEmpty() ? options.baseName.trim()
                                                                                         : juce::String::fromUTF8 ("Sin título"));
        tags.year = now.formatted ("%Y");
       #ifdef JUCE_APPLICATION_VERSION_STRING
        tags.comment = "VstPlayer " JUCE_APPLICATION_VERSION_STRING ", " + now.formatted ("%Y-%m-%d %H:%M:%S");
       #else
        tags.comment = "VstPlayer, " + now.formatted ("%Y-%m-%d %H:%M:%S");
       #endif

        juce::String error;
        mp3 = Mp3Writer::create (mp3Path.createOutputStream(), sampleRate, options.format, tags, error);

        if (mp3 == nullptr)
        {
            cleanUp();
            return juce::Result::fail (error + " (" + mp3Path.getFullPathName() + ")");
        }
    }

    if (wantWav)
    {
        std::unique_ptr<juce::OutputStream> stream = wavPath.createOutputStream();

        if (stream != nullptr)
        {
            juce::WavAudioFormat format;
            wav = format.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                      .withSampleRate (sampleRate)
                                                      .withNumChannels (2)
                                                      .withBitsPerSample (32)
                                                      .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        }

        if (wav == nullptr)
        {
            mp3 = nullptr;
            cleanUp();
            return juce::Result::fail ("No se pudo crear el archivo WAV: " + wavPath.getFullPathName());
        }
    }

    samplesWritten = 0;
    samplesDropped = 0;
    peak = 0.0f;
    writeFailed = false;
    deviceStopped = false;
    cancelPendingUpdate();

    auto splitWriter = std::make_unique<SplitWriter> (*this, sampleRate, std::move (mp3), std::move (wav));
    split = splitWriter.get();
    auto newThreaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (splitWriter.release(), writerThread,
                                                                                  juce::roundToInt (sampleRate * kFifoSeconds));
    newThreaded->setFlushInterval (juce::roundToInt (sampleRate * kWavFlushSeconds));

    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        threaded = std::move (newThreaded);
    }

    current = {};
    current.active = true;
    current.mp3File = mp3Path;
    current.wavFile = wavPath;
    current.format = options.format;
    current.sampleRate = sampleRate;
    current.startTime = now;
    recording = true;
    active = true;

    const auto info = getInfo();
    listeners.call ([&] (Listener& l) { l.recordingStarted (info); });
    return juce::Result::ok();
}

Recorder::Info Recorder::getInfo() const
{
    auto info = current;
    info.active = recording;
    info.seconds = current.sampleRate > 0.0 ? (double) samplesWritten.load() / current.sampleRate : 0.0;
    info.droppedSamples = samplesDropped.load();
    info.peak = peak.load();
    return info;
}

Recorder::Result Recorder::finishRecording (StopReason reason)
{
    Result result;
    result.reason = reason;

    if (! recording)
    {
        result.error = juce::String::fromUTF8 ("No se está grabando.");
        return result;
    }

    active = false;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> toFinish;

    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        toFinish = std::move (threaded);
    }

    // Writes what is left in the FIFO and closes the files (on this thread).
    toFinish = nullptr;
    split = nullptr;
    recording = false;

    result.info = getInfo();
    result.info.active = false;

    if (result.info.mp3File != juce::File())
        result.mp3Bytes = result.info.mp3File.getSize();

    if (result.info.wavFile != juce::File())
        result.wavBytes = result.info.wavFile.getSize();

    if (writeFailed.load())
        result.error = juce::String::fromUTF8 ("Error al escribir la grabación (¿disco lleno?).");
    else if (reason == StopReason::device)
        result.error = juce::String::fromUTF8 ("El dispositivo de audio se detuvo o cambió: grabación cortada.");

    // A device stop still leaves valid files with everything recorded until then.
    result.ok = ! writeFailed.load();
    current.active = false;
    return result;
}

Recorder::Result Recorder::stop (StopReason reason)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto wasRecording = recording;
    auto result = finishRecording (reason);

    if (wasRecording)
        listeners.call ([&] (Listener& l) { l.recordingStopped (result); });

    return result;
}

void Recorder::stopSilently()
{
    finishRecording (StopReason::shutdown);
}

void Recorder::processBlock (const float* const* outputs, int numOutputs, int numSamples) noexcept
{
    if (! active.load (std::memory_order_acquire) || numOutputs <= 0 || outputs == nullptr || outputs[0] == nullptr)
        return;

    const juce::SpinLock::ScopedTryLockType lock (writerLock);

    if (! lock.isLocked() || threaded == nullptr)
        return;

    const float* channels[2] = { outputs[0], numOutputs > 1 && outputs[1] != nullptr ? outputs[1] : outputs[0] };

    if (threaded->write (channels, numSamples))
        samplesWritten.fetch_add (numSamples);
    else
        samplesDropped.fetch_add (numSamples);

    auto blockPeak = 0.0f;

    for (auto* ch : channels)
        for (int i = 0; i < numSamples; ++i)
            blockPeak = juce::jmax (blockPeak, std::abs (ch[i]));

    auto previous = peak.load();
    while (blockPeak > previous && ! peak.compare_exchange_weak (previous, blockPeak)) {}
}

void Recorder::audioDeviceStopped() noexcept
{
    if (! active.exchange (false))
        return;

    deviceStopped = true;
    triggerAsyncUpdate();
}

void Recorder::handleAsyncUpdate()
{
    if (! recording)
        return;

    if (writeFailed.load())
        stop (StopReason::error);
    else if (deviceStopped.load())
        stop (StopReason::device);
}

} // namespace vstplayer
