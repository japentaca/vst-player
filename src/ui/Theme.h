#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vstplayer
{

// Colours of the dark theme (stage 6). Everything else comes from the LookAndFeel.
namespace theme
{
const juce::Colour background   { 0xff1a1c20 }; // window
const juce::Colour bar          { 0xff23262c }; // top bar, status bar, keyboard header
const juce::Colour row          { 0xff2a2e35 };
const juce::Colour rowSelected  { 0xff2c3e57 };
const juce::Colour rowMissing   { 0xff4a2024 };
const juce::Colour outline      { 0xff3a3f48 };
const juce::Colour text         { 0xffe8e9eb };
const juce::Colour textDim      { 0xff9aa1ab };
const juce::Colour accent       { 0xff4aa3ff };
const juce::Colour ok           { 0xff57d16b };
const juce::Colour warning      { 0xffffa53d };
const juce::Colour error        { 0xffff5c5c };
const juce::Colour panic        { 0xffb3262b };
const juce::Colour mute         { 0xffd9a521 };
} // namespace theme

// LookAndFeel_V4 with a dark colour scheme and a few tweaks (flat buttons, toggle colours).
// Default font, no external icons. Installed as the default look and feel in Main.cpp, so the
// plugin list, the dialogs and the MIDI ports window use it too.
class DarkLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    DarkLookAndFeel();

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

} // namespace vstplayer
