#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>

namespace vstplayer
{

class AppSettings;

// Knows which VST3 plugins are installed.
// - AudioPluginFormatManager with VST3PluginFormat only.
// - KnownPluginList persisted in %APPDATA%\VstPlayer\plugins.xml (saved on every change).
// - Scanning is done out of process: every candidate file is opened by a child
//   "VstPlayer.exe --scan-one <file> <result.xml>". A plugin that crashes or hangs only
//   kills the child; the file gets blacklisted and the host keeps running.
// - Search path: the default VST3 folders (JUCE: %LOCALAPPDATA%\Programs\Common\VST3 and
//   %CommonProgramFiles%\VST3) plus extra folders stored in settings.xml (key
//   "pluginExtraFolders", FileSearchPath string, ';'-separated). Extra folders are kept even
//   while they do not exist (e.g. an unplugged external drive); only the scan skips them.
class PluginManager final : private juce::ChangeListener
{
public:
    explicit PluginManager (AppSettings& settings);
    ~PluginManager() override;

    juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }
    juce::KnownPluginList& getKnownPluginList() noexcept { return knownPlugins; }
    juce::AudioPluginFormat& getVst3Format();

    static juce::File getPluginListFile();
    static juce::File getDeadMansPedalFile();
    // One line per out-of-process probe (result, exit code, time).
    static juce::File getScanLogFile();

    // Default VST3 locations (fixed, cannot be removed).
    juce::FileSearchPath getDefaultFolders();
    // Default VST3 locations + extra folders from settings, only the ones that exist now.
    juce::FileSearchPath getSearchPath();
    // Extra folders from settings, as stored (existing or not).
    juce::FileSearchPath getExtraFolders() const;
    // Stores the extra folders and writes settings.xml.
    void setExtraFolders (const juce::FileSearchPath& folders);

    enum class AddFolderResult
    {
        added,
        alreadyPresent, // same folder, or inside / containing a folder already searched
        notFound,       // not an existing directory
        unsuitable      // drive root, Windows, Program Files, Common Files, user profile root
    };

    // Adds an extra folder and writes settings.xml. Does not scan.
    AddFolderResult addExtraFolder (const juce::File& folder);
    static juce::String describe (AddFolderResult result, const juce::File& folder);

    // Removes an extra folder (writes settings.xml) and drops from the known list the plugins
    // and blacklisted files under it that no remaining search folder covers. Returns the number
    // of list entries removed, or -1 if `folder` is not an extra folder.
    int removeExtraFolder (const juce::File& folder);

    // .vst3 candidates (bundles / files) under `path`, recursive. Runs on the calling thread;
    // fast for plugin folders (it only lists directories, it does not open plugins).
    juce::StringArray findPluginFiles (const juce::FileSearchPath& path);

    // Writes plugins.xml. Returns false on I/O error.
    bool save();

    // Number of child scanner processes launched since startup (diagnostics).
    int getNumChildScans() const noexcept { return childScanCount.load(); }

    struct ScanReport
    {
        int candidateFiles = 0;
        int childProcesses = 0;
        juce::StringArray failedFiles;
        juce::StringArray newlyBlacklisted;
    };

    // Synchronous scan of the whole search path (command-line mode). Files already in the
    // list and up to date, or blacklisted, are skipped unless `force` clears the list first.
    ScanReport scanBlocking (bool force, const std::function<void (const juce::String&)>& log);

    // Child process entry point for "--scan-one <file> <outXml>". Returns the process exit code:
    // 0 = plugin types found and written, 2 = no plugin types in the file, 3 = bad arguments.
    static int runScanChild (const juce::String& fileOrIdentifier, const juce::File& outputFile);

    static constexpr int kChildExitOk = 0;
    static constexpr int kChildExitNoPlugins = 2;
    static constexpr int kChildExitBadArgs = 3;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    AppSettings& settings;
    std::atomic<int> childScanCount { 0 };

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginManager)
};

} // namespace vstplayer
