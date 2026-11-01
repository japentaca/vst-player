#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace vstplayer
{

class AppSettings;
class PluginManager;

// "Plugins…" dialog: JUCE PluginListComponent (scan, remove, blacklist) with a table showing
// name, manufacturer, version, instrument yes/no, category and path, plus a "Escanear" button.
// The scan folder list shown by the scan dialog is seeded from PluginManager::getSearchPath();
// any folder the user adds there is stored back as an extra folder when the window closes.
// Scanning runs on background threads, each probe in a child process (see PluginManager).
class PluginListWindow final : public juce::DocumentWindow
{
public:
    PluginListWindow (PluginManager& plugins, AppSettings& settings);
    ~PluginListWindow() override;

    void closeButtonPressed() override;

    // Same as pressing "Escanear" (opens the folder chooser, then scans).
    void startScan();
    // Scans exactly these files, without the folder chooser (tests). Background threads, with
    // JUCE's progress window.
    void scanFiles (const juce::StringArray& files);
    bool isScanning() const;

private:
    class Content;

    void syncExtraFolders();

    PluginManager& plugins;
    AppSettings& settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginListWindow)
};

} // namespace vstplayer
