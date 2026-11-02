#include "engine/Mp3Writer.h"

#include <lame.h>

#include <cstdarg>
#include <vector>

namespace vstplayer
{

namespace
{
// LAME reports to stderr by default: keep it quiet (errors come back as return codes).
void silentReport (const char*, va_list) {}

lame_global_flags* asLame (void* p) { return static_cast<lame_global_flags*> (p); }

// UTF-16 with BOM, zero-terminated, as id3tag_set_textinfo_utf16 wants it.
std::vector<unsigned short> toUtf16WithBom (const juce::String& text)
{
    std::vector<unsigned short> out { 0xfeff };

    for (auto p = text.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = (juce::uint32) p.getAndAdvance();

        if (c >= 0x10000)
        {
            const auto v = c - 0x10000;
            out.push_back ((unsigned short) (0xd800 + (v >> 10)));
            out.push_back ((unsigned short) (0xdc00 + (v & 0x3ff)));
        }
        else
        {
            out.push_back ((unsigned short) c);
        }
    }

    out.push_back (0);
    return out;
}

constexpr int kChunk = 8192;                     // samples per lame_encode_buffer call
constexpr int kMp3BufferSize = 5 * kChunk / 4 + 7200; // worst case for kChunk samples (lame.h)
} // namespace

//==============================================================================
int RecordingFormat::bitrateKbps() const noexcept
{
    switch (id)
    {
        case Id::cbr192: return 192;
        case Id::cbr256: return 256;
        case Id::cbr320: return 320;
        case Id::vbr0:
        case Id::vbr2:
        case Id::wavOnly: break;
    }

    return 0;
}

int RecordingFormat::vbrQuality() const noexcept
{
    return id == Id::vbr2 ? 2 : 0;
}

juce::String RecordingFormat::getKey() const
{
    switch (id)
    {
        case Id::cbr192:  return "cbr192";
        case Id::cbr256:  return "cbr256";
        case Id::cbr320:  return "cbr320";
        case Id::vbr0:    return "vbr0";
        case Id::vbr2:    return "vbr2";
        case Id::wavOnly: return "wav";
    }

    return "cbr256";
}

juce::String RecordingFormat::getLabel() const
{
    switch (id)
    {
        case Id::cbr192:  return "MP3 192 kbps CBR";
        case Id::cbr256:  return "MP3 256 kbps CBR";
        case Id::cbr320:  return "MP3 320 kbps CBR";
        case Id::vbr0:    return juce::String::fromUTF8 ("MP3 VBR V0 (~245 kbps, la mejor VBR)");
        case Id::vbr2:    return "MP3 VBR V2 (~190 kbps)";
        case Id::wavOnly: return juce::String::fromUTF8 ("Sólo WAV (sin MP3)");
    }

    return {};
}

juce::Array<RecordingFormat> RecordingFormat::all()
{
    juce::Array<RecordingFormat> formats;

    for (auto id : { Id::cbr256, Id::cbr320, Id::cbr192, Id::vbr0, Id::vbr2, Id::wavOnly })
        formats.add (RecordingFormat { id });

    return formats;
}

RecordingFormat RecordingFormat::fromKey (const juce::String& key, bool* known)
{
    for (auto f : all())
    {
        if (f.getKey().equalsIgnoreCase (key.trim()))
        {
            if (known != nullptr)
                *known = true;

            return f;
        }
    }

    if (known != nullptr)
        *known = false;

    return { kDefault };
}

//==============================================================================
std::unique_ptr<Mp3Writer> Mp3Writer::create (std::unique_ptr<juce::OutputStream> stream, double sampleRate, RecordingFormat format,
                                              const Tags& tags, juce::String& error)
{
    if (stream == nullptr)
    {
        error = "No se pudo crear el archivo MP3.";
        return nullptr;
    }

    auto* gf = lame_init();

    if (gf == nullptr)
    {
        error = "No se pudo inicializar el encoder MP3 (LAME).";
        return nullptr;
    }

    lame_set_errorf (gf, silentReport);
    lame_set_debugf (gf, silentReport);
    lame_set_msgf (gf, silentReport);

    const auto inRate = juce::roundToInt (sampleRate);
    lame_set_in_samplerate (gf, inRate);
    lame_set_num_channels (gf, 2);
    lame_set_mode (gf, JOINT_STEREO);
    lame_set_quality (gf, 0); // best (slowest) algorithm

    // MPEG-1 Layer III goes up to 48 kHz: higher device rates are resampled by LAME.
    if (inRate > 48000)
        lame_set_out_samplerate (gf, inRate % 11025 == 0 ? 44100 : 48000);

    if (format.isVbr())
    {
        lame_set_VBR (gf, vbr_default); // the new VBR (mtrh)
        lame_set_VBR_quality (gf, (float) format.vbrQuality());
    }
    else
    {
        lame_set_VBR (gf, vbr_off);
        lame_set_brate (gf, format.bitrateKbps());
    }

    lame_set_bWriteVbrTag (gf, 1); // "Info" / "Xing" frame, also for CBR (exact duration, gapless)

    id3tag_init (gf);
    id3tag_v2_only (gf);

    if (tags.title.isNotEmpty())
    {
        const auto title = toUtf16WithBom (tags.title);
        id3tag_set_textinfo_utf16 (gf, "TIT2", title.data());
    }

    if (tags.year.isNotEmpty())
        id3tag_set_year (gf, tags.year.toRawUTF8());

    if (tags.comment.isNotEmpty())
        id3tag_set_comment (gf, tags.comment.toRawUTF8());

    // The tag is written here (lame_get_id3v2_tag), not inside the first frames, so that the
    // info frame offset is known.
    lame_set_write_id3tag_automatic (gf, 0);

    if (lame_init_params (gf) < 0)
    {
        lame_close (gf);
        error = "El encoder MP3 no acepta " + juce::String (inRate) + " Hz con " + format.getLabel() + ".";
        return nullptr;
    }

    std::unique_ptr<Mp3Writer> writer (new Mp3Writer (stream.release(), sampleRate, gf));

    const auto tagSize = lame_get_id3v2_tag (gf, nullptr, 0);

    if (tagSize > 0)
    {
        juce::HeapBlock<unsigned char> tag (tagSize);
        const auto written = lame_get_id3v2_tag (gf, tag.get(), tagSize);

        if (written > 0 && ! writer->writeBytes (tag.get(), written))
        {
            error = "No se pudo escribir el archivo MP3.";
            return nullptr;
        }

        writer->tagOffset = (juce::int64) written;
    }

    return writer;
}

Mp3Writer::Mp3Writer (juce::OutputStream* stream, double rate, void* lameHandle)
    : AudioFormatWriter (stream, "MP3 file", rate, 2u, 32u),
      lame (lameHandle)
{
    usesFloatingPointData = true;
    outputSampleRate = lame_get_out_samplerate (asLame (lame));
    mp3BufferSize = kMp3BufferSize;
    mp3Buffer.malloc ((size_t) mp3BufferSize);
}

Mp3Writer::~Mp3Writer()
{
    finish();
}

bool Mp3Writer::writeBytes (const unsigned char* data, size_t size)
{
    if (size == 0)
        return true;

    if (output == nullptr || ! output->write (data, size))
    {
        failed = true;
        return false;
    }

    return true;
}

bool Mp3Writer::write (const int** samplesToWrite, int numSamples)
{
    if (finished || numSamples <= 0)
        return ! failed;

    auto* left = reinterpret_cast<const float*> (samplesToWrite[0]);
    auto* right = samplesToWrite[1] != nullptr ? reinterpret_cast<const float*> (samplesToWrite[1]) : left;

    for (int start = 0; start < numSamples; start += kChunk)
    {
        const auto n = juce::jmin (kChunk, numSamples - start);
        const auto bytes = lame_encode_buffer_ieee_float (asLame (lame), left + start, right + start, n, mp3Buffer.get(), mp3BufferSize);

        if (bytes < 0)
        {
            failed = true;
            return false;
        }

        if (! writeBytes (mp3Buffer.get(), (size_t) bytes))
            return false;
    }

    return true;
}

void Mp3Writer::finish()
{
    if (finished)
        return;

    finished = true;
    auto* gf = asLame (lame);

    const auto bytes = lame_encode_flush (gf, mp3Buffer.get(), mp3BufferSize);

    if (bytes > 0)
        writeBytes (mp3Buffer.get(), (size_t) bytes);

    // Final info frame (duration, frame count, seek table, encoder delay / padding) over the
    // placeholder LAME wrote as the first frame.
    unsigned char frame[2880];
    const auto frameSize = lame_get_lametag_frame (gf, frame, sizeof (frame));

    if (frameSize > 0 && frameSize <= sizeof (frame) && output != nullptr && ! failed)
    {
        output->flush();
        const auto end = output->getPosition();

        if (output->setPosition (tagOffset))
        {
            writeBytes (frame, frameSize);
            output->setPosition (end);
        }
    }

    if (output != nullptr)
        output->flush();

    lame_close (gf);
    lame = nullptr;
}

} // namespace vstplayer
