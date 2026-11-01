#include "engine/PluginManager.h"

#include "app/AppSettings.h"

namespace vstplayer
{

namespace
{
constexpr const char* kExtraFoldersKey = "pluginExtraFolders";
constexpr const char* kScanResultTag = "SCANRESULT";
constexpr int kChildTimeoutMs = 120 * 1000;
constexpr int kChildPollMs = 20;

// Appends one line per probe to %APPDATA%\VstPlayer\plugin-scan.log (diagnostics).
void appendScanLog (const juce::String& line)
{
    static juce::CriticalSection lock;
    const juce::ScopedLock sl (lock);

    const auto file = PluginManager::getScanLogFile();
    file.getParentDirectory().createDirectory();
    file.appendText (juce::Time::getCurrentTime().toString (true, true, true, true) + "  " + line + juce::newLine,
                     false, false, nullptr);
}

// Runs every plugin probe in a separate VstPlayer.exe process.
// findPluginTypesFor() returns false (=> the file is blacklisted by KnownPluginList) when the
// child crashed, hung past the timeout or found no plugin in the file.
class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    explicit OutOfProcessScanner (std::atomic<int>& counter) : childCount (counter) {}

    bool findPluginTypesFor (juce::AudioPluginFormat&,
                             juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override
    {
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        // Unique per probe: several scan threads run concurrently, so getNonexistentChildFile()
        // could hand the same name to two of them before either child creates the file.
        const auto resultFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getChildFile ("VstPlayer-scan-" + juce::Uuid().toString() + ".xml");

        juce::ChildProcess child;
        const juce::StringArray args { exe.getFullPathName(), "--scan-one", fileOrIdentifier,
                                       resultFile.getFullPathName() };

        if (! child.start (args, 0))
        {
            // Could not even launch the scanner: do not blame the plugin.
            appendScanLog ("NO SE PUDO LANZAR el proceso hijo: " + fileOrIdentifier);
            return true;
        }

        childCount.fetch_add (1);

        const auto startMs = juce::Time::getMillisecondCounter();

        while (child.isRunning())
        {
            if (shouldExit())
            {
                child.kill();
                child.waitForProcessToFinish (2000);
                resultFile.deleteFile();
                appendScanLog ("cancelado: " + fileOrIdentifier);
                return true; // scan cancelled by the user: not the plugin's fault
            }

            if (juce::Time::getMillisecondCounter() - startMs > (juce::uint32) kChildTimeoutMs)
            {
                child.kill();
                child.waitForProcessToFinish (2000);
                resultFile.deleteFile();
                appendScanLog ("TIMEOUT (" + juce::String (kChildTimeoutMs / 1000) + " s), desactivado: " + fileOrIdentifier);
                return false; // hung
            }

            juce::Thread::sleep (kChildPollMs);
        }

        const auto exitCode = child.getExitCode();
        bool ok = false;

        if (exitCode == (juce::uint32) PluginManager::kChildExitOk)
        {
            if (auto xml = juce::parseXMLIfTagMatches (resultFile, kScanResultTag))
            {
                for (auto* e : xml->getChildIterator())
                {
                    auto desc = std::make_unique<juce::PluginDescription>();

                    if (! desc->loadFromXml (*e))
                        continue;

                    // JUCE reports the inner DLL (Bundle.vst3\Contents\x86_64-win\X.vst3) for
                    // bundles without moduleinfo.json, but the scanner enumerates the bundle
                    // folder. Store the bundle path so the next scan sees the listing as up
                    // to date instead of probing the plugin again.
                    const juce::File scanned (fileOrIdentifier);

                    if (scanned.isDirectory() && juce::File (desc->fileOrIdentifier).isAChildOf (scanned))
                    {
                        desc->fileOrIdentifier = scanned.getFullPathName();
                        desc->lastFileModTime = scanned.getLastModificationTime();
                    }

                    result.add (desc.release());
                }

                ok = ! result.isEmpty();
            }
        }

        resultFile.deleteFile();

        appendScanLog (juce::String (ok ? "ok" : "FALLO") + " exit=0x" + juce::String::toHexString ((int) exitCode)
                       + " tipos=" + juce::String (result.size())
                       + " " + juce::String (juce::Time::getMillisecondCounter() - startMs) + " ms: " + fileOrIdentifier);
        return ok;
    }

private:
    std::atomic<int>& childCount;
};
} // namespace

PluginManager::PluginManager (AppSettings& s)
    : settings (s)
{
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());

    // Keep the scan log small.
    if (getScanLogFile().getSize() > 256 * 1024)
        getScanLogFile().deleteFile();

    if (auto xml = juce::parseXML (getPluginListFile()))
        knownPlugins.recreateFromXml (*xml);

    // If a previous in-process scan crashed the host, blacklist the culprit.
    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (knownPlugins, getDeadMansPedalFile());

    knownPlugins.setCustomScanner (std::make_unique<OutOfProcessScanner> (childScanCount));
    knownPlugins.addChangeListener (this);
}

PluginManager::~PluginManager()
{
    knownPlugins.removeChangeListener (this);
    save();
}

juce::AudioPluginFormat& PluginManager::getVst3Format()
{
    auto* format = formatManager.getFormat (0);
    jassert (format != nullptr);
    return *format;
}

juce::File PluginManager::getPluginListFile()
{
    return AppSettings::getUserDataDirectory().getChildFile ("plugins.xml");
}

juce::File PluginManager::getScanLogFile()
{
    return AppSettings::getUserDataDirectory().getChildFile ("plugin-scan.log");
}

juce::File PluginManager::getDeadMansPedalFile()
{
    return AppSettings::getUserDataDirectory().getChildFile ("plugin-scan-in-progress.txt");
}

juce::FileSearchPath PluginManager::getSearchPath()
{
    auto path = getVst3Format().getDefaultLocationsToSearch();
    const auto extras = getExtraFolders();

    for (int i = 0; i < extras.getNumPaths(); ++i)
        path.addIfNotAlreadyThere (extras[i]);

    path.removeNonExistentPaths();
    return path;
}

juce::FileSearchPath PluginManager::getExtraFolders() const
{
    return juce::FileSearchPath (settings.getProperties().getValue (kExtraFoldersKey));
}

void PluginManager::setExtraFolders (const juce::FileSearchPath& folders)
{
    settings.getProperties().setValue (kExtraFoldersKey, folders.toString());
}

void PluginManager::setExtraFoldersFromFullPath (const juce::FileSearchPath& fullPath)
{
    const auto defaults = getVst3Format().getDefaultLocationsToSearch();
    juce::FileSearchPath extras;

    for (int i = 0; i < fullPath.getNumPaths(); ++i)
    {
        const auto folder = fullPath[i];
        bool isDefault = false;

        for (int j = 0; j < defaults.getNumPaths(); ++j)
            isDefault = isDefault || folder == defaults[j];

        if (! isDefault)
            extras.addIfNotAlreadyThere (folder);
    }

    setExtraFolders (extras);
}

bool PluginManager::save()
{
    const auto file = getPluginListFile();
    file.getParentDirectory().createDirectory();

    if (auto xml = knownPlugins.createXml())
        return xml->writeTo (file);

    return false;
}

PluginManager::ScanReport PluginManager::scanBlocking (bool force, const std::function<void (const juce::String&)>& log)
{
    ScanReport report;

    if (force)
    {
        knownPlugins.clear();
        knownPlugins.clearBlacklistedFiles();
    }

    const auto blacklistedBefore = knownPlugins.getBlacklistedFiles();
    const auto childrenBefore = childScanCount.load();

    auto& format = getVst3Format();
    const auto path = getSearchPath();

    if (log)
        log ("Carpetas: " + path.toString());

    juce::PluginDirectoryScanner scanner (knownPlugins, format, path, true, getDeadMansPedalFile(), false);
    report.candidateFiles = format.searchPathsForPlugins (path, true, false).size();

    juce::String current;
    for (;;)
    {
        const auto more = scanner.scanNextFile (true, current);

        if (! more)
            break;
    }

    report.failedFiles = scanner.getFailedFiles();
    report.childProcesses = childScanCount.load() - childrenBefore;

    for (auto& f : knownPlugins.getBlacklistedFiles())
        if (! blacklistedBefore.contains (f))
            report.newlyBlacklisted.add (f);

    save();
    return report;
}

int PluginManager::runScanChild (const juce::String& fileOrIdentifier, const juce::File& outputFile)
{
    if (fileOrIdentifier.isEmpty() || outputFile == juce::File())
        return kChildExitBadArgs;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;

    if (format.fileMightContainThisPluginType (fileOrIdentifier))
        format.findAllTypesForFile (found, fileOrIdentifier);

    juce::XmlElement root (kScanResultTag);

    for (auto* desc : found)
        root.addChildElement (desc->createXml().release());

    outputFile.getParentDirectory().createDirectory();
    root.writeTo (outputFile);

    return found.isEmpty() ? kChildExitNoPlugins : kChildExitOk;
}

void PluginManager::changeListenerCallback (juce::ChangeBroadcaster*)
{
    save();
}

} // namespace vstplayer
