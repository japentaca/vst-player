#pragma once

#include "ui/Theme.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace vstplayer
{

// Stereo horizontal level meter (two bars, L on top). The owner feeds it the peaks measured in a
// GainProcessor (getAndResetMeterPeak) from a UI timer; the bars fall back smoothly.
// Scale: -60 dBFS .. +6 dBFS; green, yellow above -12 dBFS, red above -1 dBFS (clip).
class LevelMeter final : public juce::Component
{
public:
    LevelMeter()
    {
        setInterceptsMouseClicks (false, false);
    }

    // Linear peaks since the previous call (one call per UI tick).
    void pushPeaks (float left, float right)
    {
        const auto update = [] (float& shown, float peak)
        {
            const auto db = peak > 0.0f ? juce::Decibels::gainToDecibels (peak, kMinDb) : kMinDb;
            shown = db >= shown ? db : juce::jmax (db, shown - kFallDbPerTick);
            return shown;
        };

        const auto oldL = levelDb[0], oldR = levelDb[1];
        update (levelDb[0], left);
        update (levelDb[1], right);

        if (std::abs (oldL - levelDb[0]) > 0.05f || std::abs (oldR - levelDb[1]) > 0.05f)
            repaint();
    }

    void reset()
    {
        levelDb[0] = levelDb[1] = kMinDb;
        repaint();
    }

    float getLevelDb (int channel) const noexcept { return levelDb[channel == 0 ? 0 : 1]; }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff111316));
        g.fillRoundedRectangle (area, 2.0f);

        const auto inner = area.reduced (1.5f);
        const auto barHeight = (inner.getHeight() - 1.0f) * 0.5f;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto bar = juce::Rectangle<float> (inner.getX(), inner.getY() + (float) ch * (barHeight + 1.0f), inner.getWidth(), barHeight);
            const auto proportion = juce::jlimit (0.0f, 1.0f, (levelDb[ch] - kMinDb) / (kMaxDb - kMinDb));

            if (proportion <= 0.0f)
                continue;

            const auto colour = levelDb[ch] > -1.0f ? theme::error : levelDb[ch] > -12.0f ? juce::Colour (0xffe8d44d) : theme::ok;
            g.setColour (colour);
            g.fillRect (bar.withWidth (bar.getWidth() * proportion));
        }

        // 0 dBFS mark.
        const auto zeroX = inner.getX() + inner.getWidth() * (0.0f - kMinDb) / (kMaxDb - kMinDb);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawVerticalLine (juce::roundToInt (zeroX), inner.getY(), inner.getBottom());
    }

    static constexpr float kMinDb = -60.0f;
    static constexpr float kMaxDb = 6.0f;
    static constexpr float kFallDbPerTick = 1.5f; // ~45 dB/s at 30 Hz

private:
    float levelDb[2] { kMinDb, kMinDb };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};

} // namespace vstplayer
