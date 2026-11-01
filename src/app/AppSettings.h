#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace vstplayer
{

// Persistent user settings stored in %APPDATA%\VstPlayer\settings.xml.
class AppSettings
{
public:
    AppSettings();
    ~AppSettings();

    juce::PropertiesFile& getProperties();
    juce::File getSettingsFile() const;

    // Window state as produced by ResizableWindow::getWindowStateAsString().
    juce::String getMainWindowState() const;
    void setMainWindowState (const juce::String& state);

    // Folder that holds every per-user file of the app (%APPDATA%\VstPlayer).
    static juce::File getUserDataDirectory();

    void flush();

private:
    std::unique_ptr<juce::PropertiesFile> properties;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppSettings)
};

} // namespace vstplayer
