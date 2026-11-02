#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>

namespace vstplayer
{

// Output format of a recording. Keys are the values stored in settings.xml and taken by the API
// ("format"); "wav" = no MP3, only the WAV.
struct RecordingFormat
{
    enum class Id
    {
        cbr256,
        cbr320,
        cbr192,
        vbr0,
        vbr2,
        wavOnly
    };

    Id id = Id::cbr256;

    bool isMp3() const noexcept { return id != Id::wavOnly; }
    bool isVbr() const noexcept { return id == Id::vbr0 || id == Id::vbr2; }
    int bitrateKbps() const noexcept; // CBR bitrate, 0 for VBR / WAV
    int vbrQuality() const noexcept;  // LAME -V value (0 = best)

    juce::String getKey() const;   // "cbr256"
    juce::String getLabel() const; // "MP3 256 kbps CBR" (UI, Spanish)

    static RecordingFormat fromKey (const juce::String& key, bool* known = nullptr);
    static juce::Array<RecordingFormat> all();
    static constexpr Id kDefault = Id::cbr256;

    bool operator== (const RecordingFormat& other) const noexcept { return id == other.id; }
};

// AudioFormatWriter that encodes stereo float audio to MP3 with LAME (libmp3lame, linked
// statically). Quality: LAME's best algorithm (-q 0), joint stereo, CBR or VBR (new VBR, -V n);
// sample rates above 48 kHz are resampled by LAME to 48 / 44.1 kHz (MPEG-1 Layer III limit).
// ID3v2 tag (title, year, comment) at the start; the LAME "Info" / "Xing" frame (duration, seek
// table, encoder delay) is rewritten over the first frame when the writer is destroyed, so the
// output stream must be seekable (FileOutputStream).
// Not thread-safe: write() and the destructor on the same thread (ThreadedWriter's).
class Mp3Writer final : public juce::AudioFormatWriter
{
public:
    struct Tags
    {
        juce::String title;
        juce::String year;    // "2026"
        juce::String comment; // ASCII
    };

    // Null (and `error` set) if LAME rejects the settings. Takes ownership of the stream.
    static std::unique_ptr<Mp3Writer> create (std::unique_ptr<juce::OutputStream> stream, double sampleRate, RecordingFormat format,
                                              const Tags& tags, juce::String& error);

    ~Mp3Writer() override;

    bool write (const int** samplesToWrite, int numSamples) override;

    // True once a write to the stream failed (disk full...). Any thread.
    bool hasFailed() const noexcept { return failed.load(); }
    // Output sample rate (equal to the input unless LAME resamples).
    int getOutputSampleRate() const noexcept { return outputSampleRate; }

private:
    Mp3Writer (juce::OutputStream* stream, double sampleRate, void* lameHandle);
    bool writeBytes (const unsigned char* data, size_t size);
    void finish();

    void* lame = nullptr; // lame_global_flags*
    juce::HeapBlock<unsigned char> mp3Buffer;
    int mp3BufferSize = 0;
    juce::int64 tagOffset = 0; // where the info frame goes (after the ID3v2 tag)
    int outputSampleRate = 0;
    std::atomic<bool> failed { false };
    bool finished = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Mp3Writer)
};

} // namespace vstplayer
