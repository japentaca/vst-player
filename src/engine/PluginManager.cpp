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

PluginManager::PluginManager (AppSettings& s, bool shouldPersist)
    : settings (s), persist (shouldPersist)
{
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());

    // Keep the scan log small.
    if (persist && getScanLogFile().getSize() > 256 * 1024)
        getScanLogFile().deleteFile();

    if (auto xml = juce::parseXML (getPluginListFile()))
        knownPlugins.recreateFromXml (*xml);

    // If a previous in-process scan crashed the host, blacklist the culprit.
    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (knownPlugins, getDeadMansPedalFile());

    knownPlugins.setCustomScanner (std::make_unique<OutOfProcessScanner> (childScanCount));

    if (! persist)
        return; // read-only: no listener (nothing is saved), settings untouched

    // Up to v0.1.0 the scan folders went through JUCE's folder dialog, which kept its own copy
    // of the list under this key. The folders now live only in "pluginExtraFolders".
    settings.getProperties().removeValue ("lastPluginScanPath_VST3");
    knownPlugins.addChangeListener (this);
}

PluginManager::~PluginManager()
{
    knownPlugins.removeChangeListener (this);

    if (persist)
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

juce::FileSearchPath PluginManager::getDefaultFolders()
{
    return getVst3Format().getDefaultLocationsToSearch();
}

juce::FileSearchPath PluginManager::getSearchPath()
{
    auto path = getDefaultFolders();
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
    if (folders.getNumPaths() == 0)
        settings.getProperties().removeValue (kExtraFoldersKey);
    else
        settings.getProperties().setValue (kExtraFoldersKey, folders.toString());

    settings.flush();
}

namespace
{
bool isSameOrInside (const juce::File& f, const juce::File& folder)
{
    return f == folder || f.isAChildOf (folder);
}

// Folders that would make the recursive search walk half the disk (JUCE's own scan dialog
// warns about the same kind of folders).
bool isUnsuitableFolder (const juce::File& folder)
{
    if (folder.getParentDirectory() == folder) // drive root: C:\, D:\ ...
        return true;

    const auto windowsDir = juce::File::getSpecialLocation (juce::File::windowsSystemDirectory).getParentDirectory();
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    const juce::File unsuitable[] = {
        windowsDir,                                                                  // C:\Windows
        juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory),    // C:\Program Files
        juce::File::getSpecialLocation (juce::File::globalApplicationsDirectoryX86), // C:\Program Files (x86)
        juce::File::getSpecialLocation (juce::File::windowsProgramFilesCommon),      // ...\Common Files
        home,                                                                        // C:\Users\<user>
        home.getParentDirectory(),                                                   // C:\Users
    };

    for (auto& f : unsuitable)
        if (f != juce::File() && folder == f)
            return true;

    return folder.isAChildOf (windowsDir);
}
} // namespace

PluginManager::AddFolderResult PluginManager::addExtraFolder (const juce::File& folder)
{
    if (! folder.isDirectory())
        return AddFolderResult::notFound;

    if (isUnsuitableFolder (folder))
        return AddFolderResult::unsuitable;

    auto all = getDefaultFolders();
    const auto extras = getExtraFolders();

    for (int i = 0; i < extras.getNumPaths(); ++i)
        all.addIfNotAlreadyThere (extras[i]);

    // The search is recursive: a folder inside one already searched adds nothing, and one that
    // contains a searched folder would scan it twice. Both are refused.
    for (int i = 0; i < all.getNumPaths(); ++i)
        if (isSameOrInside (folder, all[i]) || all[i].isAChildOf (folder))
            return AddFolderResult::alreadyPresent;

    auto updated = extras;
    updated.add (folder);
    setExtraFolders (updated);
    return AddFolderResult::added;
}

juce::String PluginManager::describe (AddFolderResult result, const juce::File& folder)
{
    const auto name = folder.getFullPathName();

    switch (result)
    {
        case AddFolderResult::added:
            return "Carpeta agregada: " + name;
        case AddFolderResult::alreadyPresent:
            return juce::String::fromUTF8 ("Ya se busca en esa carpeta (es una de la lista, está dentro de una o la contiene): ") + name;
        case AddFolderResult::notFound:
            return "No existe la carpeta: " + name;
        case AddFolderResult::unsuitable:
            return juce::String::fromUTF8 ("No se puede agregar la raíz de un disco ni una carpeta del sistema "
                                           "(se recorrería el disco entero): ") + name;
    }

    return {};
}

int PluginManager::removeExtraFolder (const juce::File& folder)
{
    const auto extras = getExtraFolders();
    juce::FileSearchPath remaining;
    bool found = false;

    for (int i = 0; i < extras.getNumPaths(); ++i)
    {
        if (extras[i] == folder)
            found = true;
        else
            remaining.add (extras[i]);
    }

    if (! found)
        return -1;

    setExtraFolders (remaining);

    auto stillSearched = getDefaultFolders();

    for (int i = 0; i < remaining.getNumPaths(); ++i)
        stillSearched.addIfNotAlreadyThere (remaining[i]);

    const auto isOrphan = [&] (const juce::String& path)
    {
        if (! juce::File::isAbsolutePath (path))
            return false;

        const juce::File f (path);

        if (! isSameOrInside (f, folder))
            return false;

        for (int i = 0; i < stillSearched.getNumPaths(); ++i)
            if (isSameOrInside (f, stillSearched[i]))
                return false;

        return true;
    };

    int removed = 0;

    for (auto& d : knownPlugins.getTypes())
    {
        if (isOrphan (d.fileOrIdentifier))
        {
            knownPlugins.removeType (d);
            ++removed;
        }
    }

    for (auto& f : knownPlugins.getBlacklistedFiles())
    {
        if (isOrphan (f))
        {
            knownPlugins.removeFromBlacklist (f);
            ++removed;
        }
    }

    save();
    return removed;
}

juce::StringArray PluginManager::findPluginFiles (const juce::FileSearchPath& path)
{
    return getVst3Format().searchPathsForPlugins (path, true, false);
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
