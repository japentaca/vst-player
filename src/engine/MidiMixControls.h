#pragma once

#include <atomic>
#include <cmath>
#include <memory>

namespace vstplayer
{

// Volume (CC7) and pan (CC10) received by one instance, shared by its MidiSourceProcessor
// (writes them on the audio thread, and does not pass those two controllers on to the plugin)
// and its GainProcessor (applies them on top of the row's gain). -1 = not received yet:
// unity gain, centered. Not saved in the project: it is MIDI state, like a plugin's
// controllers (the sender, e.g. carrilion, sends it again).
struct MidiMixControls
{
    std::atomic<int> volumeCc { -1 }; // 0..127
    std::atomic<int> panCc { -1 };    // 0..127, 64 = center

    // Linear amplitude: value / 127 (carrilion sends its channel's linear volume * 127).
    float volumeGain() const noexcept
    {
        const auto v = volumeCc.load();
        return v < 0 ? 1.0f : (float) v / 127.0f;
    }

    // -1 (left) .. 1 (right); 64 is exactly the center (0..64 and 64..127 scaled apart).
    float pan() const noexcept
    {
        const auto v = panCc.load();

        if (v < 0 || v == 64)
            return 0.0f;

        return v < 64 ? (float) (v - 64) / 64.0f : (float) (v - 64) / 63.0f;
    }

    // Balance of a stereo signal as Web Audio's StereoPannerNode does it (what carrilion's
    // own Tone.Panner does), so a channel sounds placed the same in both. Gains of the side
    // that receives the other one's signal: out = { l*ll + r*rl, l*lr + r*rr }.
    struct StereoGains
    {
        float ll = 1.0f, rl = 0.0f, lr = 0.0f, rr = 1.0f;
    };

    static StereoGains stereoGains (float p) noexcept
    {
        constexpr float halfPi = 1.57079632679f;
        StereoGains g;

        if (p <= 0.0f)
        {
            const auto x = (p + 1.0f) * halfPi;
            g.rl = std::cos (x); // right moves into the left
            g.rr = std::sin (x);
        }
        else
        {
            const auto x = p * halfPi;
            g.ll = std::cos (x);
            g.lr = std::sin (x); // left moves into the right
            g.rl = 0.0f;
        }

        return g;
    }
};

} // namespace vstplayer
