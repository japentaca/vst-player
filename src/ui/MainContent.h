#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>

namespace vstplayer
{

class AudioEngine;
class PluginManager;

// Provisional main window content (stage 2): audio status, plugin count and the test tone
// button. Replaced by the definitive UI in stage 6.
class MainContent final : public juce::Component,
                          private juce::ChangeListener
{
public:
    MainContent (AudioEngine& engine, PluginManager& plugins);
    ~MainContent() override;

    // Last audio device error (empty clears it). Cleared automatically when the device changes.
    void setAudioError (const juce::String& message);

    std::function<void()> onShowAudioSettings;
    std::function<void()> onShowPlugins;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();

    AudioEngine& engine;
    PluginManager& plugins;

    juce::Label titleLabel;
    juce::Label audioLabel;
    juce::Label pluginsLabel;
    juce::Label errorLabel;
    juce::TextButton toneButton;
    juce::TextButton audioButton;
    juce::TextButton pluginsButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainContent)
};

} // namespace vstplayer
