#include "ui/Theme.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace vstplayer
{

DarkLookAndFeel::DarkLookAndFeel()
    : LookAndFeel_V4 (LookAndFeel_V4::ColourScheme {
          theme::background,         // windowBackground
          theme::row,                // widgetBackground
          theme::bar,                // menuBackground
          theme::outline,            // outline
          theme::text,               // defaultText
          juce::Colour (0xff3a404a), // defaultFill
          juce::Colours::white,      // highlightedText
          theme::accent.darker (0.3f), // highlightedFill
          theme::text })             // menuText
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::background);
    setColour (juce::DocumentWindow::backgroundColourId, theme::background);
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff353a43));
    setColour (juce::TextButton::buttonOnColourId, theme::accent.darker (0.2f));
    setColour (juce::TextButton::textColourOffId, theme::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff30343c));
    setColour (juce::ComboBox::outlineColourId, theme::outline);
    setColour (juce::ComboBox::textColourId, theme::text);
    setColour (juce::ComboBox::arrowColourId, theme::textDim);
    setColour (juce::Label::textColourId, theme::text);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff15171a));
    setColour (juce::TextEditor::outlineColourId, theme::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    setColour (juce::Slider::trackColourId, theme::accent.withAlpha (0.8f));
    setColour (juce::Slider::backgroundColourId, juce::Colour (0xff15171a));
    setColour (juce::Slider::thumbColourId, juce::Colour (0xffd0d4da));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxTextColourId, theme::text);
    setColour (juce::ToggleButton::tickColourId, theme::accent);
    setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff15171a));
    setColour (juce::ListBox::outlineColourId, theme::outline);
    setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff4a505a));
    setColour (juce::ProgressBar::foregroundColourId, theme::accent);
    setColour (juce::ProgressBar::backgroundColourId, juce::Colour (0xff15171a));
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff30343c));
    setColour (juce::TooltipWindow::textColourId, theme::text);
    setColour (juce::AlertWindow::backgroundColourId, theme::bar);
    setColour (juce::AlertWindow::textColourId, theme::text);
    setColour (juce::PopupMenu::backgroundColourId, theme::bar);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent.darker (0.3f));
    setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, theme::accent.withAlpha (0.7f));
    setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, theme::accent.withAlpha (0.25f));
}

juce::Font DarkLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::FontOptions (juce::jmin (15.0f, (float) buttonHeight * 0.62f));
}

void DarkLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                            bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = backgroundColour.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f);

    if (isDown)
        colour = colour.brighter (0.25f);
    else if (isHighlighted)
        colour = colour.brighter (0.12f);

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (theme::outline.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

} // namespace vstplayer
