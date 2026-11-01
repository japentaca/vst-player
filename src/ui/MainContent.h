#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>

namespace vstplayer
{

class AudioEngine;
class InstanceGraph;
class InstancePanel;
class MidiRouter;
class PluginManager;

// Provisional main window content: audio status, plugin count and the test tone button
// (stage 2) plus the instance panel with the virtual keyboard (stage 3).
// Replaced by the definitive UI in stage 6.
class MainContent final : public juce::Component,
                          private juce::ChangeListener
{
public:
    MainContent (AudioEngine& engine, PluginManager& plugins, InstanceGraph& instances, MidiRouter& router);
    ~MainContent() override;

    // Last audio device error (empty clears it). Cleared automatically when the device changes.
    void setAudioError (const juce::String& message);
    // Top line: project name (with "*" if modified) and loading progress (stage 5).
    void setProjectInfo (const juce::String& text);
    juce::String getProjectInfo() const { return titleLabel.getText(); }

    InstancePanel& getInstancePanel() noexcept { return *instancePanel; }

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
    std::unique_ptr<InstancePanel> instancePanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainContent)
};

} // namespace vstplayer
