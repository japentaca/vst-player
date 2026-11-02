#pragma once

#include "app/RecordingSettings.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace vstplayer
{

// Content of Audio > "Opciones de grabación…": format (MP3 quality or WAV only), "also keep the
// WAV", and the folder (the project's or a fixed one). Changes apply at once (onChange); the
// next recording uses them.
class RecordingSettingsComponent final : public juce::Component
{
public:
    RecordingSettingsComponent (RecordingSettings initial, std::function<juce::File (const RecordingSettings&)> resolveFolder);
    ~RecordingSettingsComponent() override;

    std::function<void (const RecordingSettings&)> onChange;

    const RecordingSettings& getSettings() const noexcept { return settings; }

    void resized() override;

private:
    void refresh();
    void changed();
    void chooseFolder();

    RecordingSettings settings;
    std::function<juce::File (const RecordingSettings&)> resolveFolder;

    juce::Label formatLabel, folderLabel, targetLabel, noteLabel;
    juce::ComboBox formatBox;
    juce::ToggleButton keepWavToggle;
    juce::ToggleButton projectFolderToggle, fixedFolderToggle;
    juce::Label fixedFolderLabel;
    juce::TextButton chooseFolderButton, openFolderButton;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecordingSettingsComponent)
};

} // namespace vstplayer
