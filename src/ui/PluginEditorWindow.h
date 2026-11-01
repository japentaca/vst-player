#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace vstplayer
{

// Window hosting the editor of one plugin instance. Uses the plugin's own editor
// (createEditorAndMakeActive, the non-deprecated createEditorIfNeeded) or, if it has none,
// a GenericAudioProcessorEditor.
// The window does not own the processor: its owner (InstancePanel) must delete the window
// before the instance is removed. Closing it only calls onCloseRequested; the owner stores
// the position and deletes the window (the instance keeps running).
class PluginEditorWindow final : public juce::DocumentWindow
{
public:
    PluginEditorWindow (juce::AudioProcessor& processor, const juce::String& title);
    ~PluginEditorWindow() override;

    void closeButtonPressed() override;

    // Called from closeButtonPressed; the callee may delete this window.
    std::function<void()> onCloseRequested;

    bool usesGenericEditor() const noexcept { return genericEditor; }
    juce::AudioProcessorEditor* getEditor() const;

private:
    juce::AudioProcessor& processor;
    bool genericEditor = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditorWindow)
};

} // namespace vstplayer
