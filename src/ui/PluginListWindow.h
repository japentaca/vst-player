#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace vstplayer
{

class AppSettings;
class PluginManager;

// "Plugins…" dialog:
// - "Carpetas de búsqueda" panel: the default VST3 folders (fixed) plus the extra folders from
//   settings, with "Agregar carpeta…" (folder chooser; the new folder is scanned right away)
//   and "Quitar carpeta" (also drops the plugins found only there from the list).
// - JUCE PluginListComponent (remove, blacklist, options) with a table showing name,
//   manufacturer, version, instrument yes/no, category and path.
// - "Escanear": scans every .vst3 under the search folders (new or changed files only), without
//   JUCE's folder dialog. Background thread, each probe in a child process (see PluginManager).
class PluginListWindow final : public juce::DocumentWindow
{
public:
    PluginListWindow (PluginManager& plugins, AppSettings& settings);
    ~PluginListWindow() override;

    void closeButtonPressed() override;

    // Same as pressing "Escanear".
    void startScan();
    // Scans exactly these files (tests). Background threads, with JUCE's progress window.
    void scanFiles (const juce::StringArray& files);
    bool isScanning() const;

    // Same as choosing `folder` with "Agregar carpeta…" (tests): adds it and, if added,
    // scans it. Returns true if the folder was added.
    bool addFolder (const juce::File& folder);
    // Same as selecting `folder` and pressing "Quitar carpeta". Returns the number of list
    // entries removed, or -1 if it is not an extra folder.
    int removeFolder (const juce::File& folder);
    // Rows of the folder panel (default + extra folders) and the status line under the table.
    int getNumFolderRows() const;
    juce::String getStatusText() const;

private:
    class Content;
    Content* getContent() const;

    PluginManager& plugins;
    AppSettings& settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginListWindow)
};

} // namespace vstplayer
