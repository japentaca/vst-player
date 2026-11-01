#include "app/AppSettings.h"

namespace vstplayer
{

namespace
{
constexpr const char* kFolderName = "VstPlayer";
constexpr const char* kMainWindowStateKey = "mainWindowState";

juce::File& userDataDirectoryOverride()
{
    static juce::File dir;
    return dir;
}

juce::PropertiesFile::Options makeOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "settings";
    options.filenameSuffix = ".xml";
    options.folderName = kFolderName;
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.commonToAllUsers = false;
    options.ignoreCaseOfKeyNames = false;
    options.millisecondsBeforeSaving = 500;
    return options;
}
} // namespace

AppSettings::AppSettings()
{
    const auto dir = getUserDataDirectory();
    dir.createDirectory();
    properties = std::make_unique<juce::PropertiesFile> (dir.getChildFile ("settings.xml"), makeOptions());
}

AppSettings::~AppSettings()
{
    flush();
}

juce::PropertiesFile& AppSettings::getProperties()
{
    return *properties;
}

juce::File AppSettings::getSettingsFile() const
{
    return properties->getFile();
}

juce::String AppSettings::getMainWindowState() const
{
    return properties->getValue (kMainWindowStateKey);
}

void AppSettings::setMainWindowState (const juce::String& state)
{
    properties->setValue (kMainWindowStateKey, state);
}

void AppSettings::setUserDataDirectoryOverride (const juce::File& dir)
{
    userDataDirectoryOverride() = dir;
}

juce::File AppSettings::getUserDataDirectory()
{
    if (userDataDirectoryOverride() != juce::File())
        return userDataDirectoryOverride();

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (kFolderName);
}

void AppSettings::flush()
{
    if (properties != nullptr && ! properties->saveIfNeeded())
    {
        DBG ("AppSettings: could not save " << properties->getFile().getFullPathName());
    }
}

} // namespace vstplayer
