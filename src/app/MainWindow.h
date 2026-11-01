#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace vstplayer
{

class AppSettings;
class AudioEngine;
class PluginManager;
class PluginListWindow;

// Main application window: menu bar (Archivo, Audio, Ayuda) and the provisional content.
class MainWindow final : public juce::DocumentWindow,
                         private juce::MenuBarModel
{
public:
    MainWindow (const juce::String& title, AppSettings& settings, AudioEngine& engine, PluginManager& plugins);
    ~MainWindow() override;

    void closeButtonPressed() override;

    // Writes the current size/position into the settings (does not flush to disk).
    void saveWindowState();

    void showAudioSettings();
    // Shows an audio device error in the status area (non-modal).
    void setAudioError (const juce::String& message);
    // Opens the plugin list window; startScan = also press "Escanear".
    void showPluginList (bool startScan = false);

private:
    enum MenuIndex
    {
        fileMenu = 0,
        audioMenu,
        helpMenu
    };

    enum CommandId
    {
        cmdQuit = 1,
        cmdAbout,
        cmdPlugins,
        cmdAudioSettings
    };

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    void showAboutBox();

    AppSettings& settings;
    AudioEngine& engine;
    PluginManager& plugins;

    std::unique_ptr<PluginListWindow> pluginListWindow;
    juce::Component::SafePointer<juce::DialogWindow> audioSettingsWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

} // namespace vstplayer
