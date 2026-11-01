#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace vstplayer
{

class AppSettings;

// Main application window: menu bar (Archivo, Audio, Ayuda) and an empty content area.
class MainWindow final : public juce::DocumentWindow,
                         private juce::MenuBarModel
{
public:
    MainWindow (const juce::String& title, AppSettings& settings);
    ~MainWindow() override;

    void closeButtonPressed() override;

    // Writes the current size/position into the settings (does not flush to disk).
    void saveWindowState();

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
        cmdAbout
    };

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    void showAboutBox();

    AppSettings& settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

} // namespace vstplayer
