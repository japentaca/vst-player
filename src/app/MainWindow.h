#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

namespace vstplayer
{

class AppSettings;
class AudioEngine;
class InstanceGraph;
class InstancePanel;
class MidiPortsWindow;
class MidiRouter;
class PluginManager;
class PluginListWindow;
class ProjectController;

// Main application window: menu bar (Archivo, Audio, Ayuda) and the provisional content.
// Archivo: Nuevo, Abrir…, Guardar, Guardar como…, Recientes, Plugins…, Salir. Every action that
// would drop the current project (Nuevo, Abrir, Recientes, Salir, close button) first asks
// about unsaved changes. Title: "VstPlayer — <proyecto>" with " *" when modified.
class MainWindow final : public juce::DocumentWindow,
                         private juce::MenuBarModel,
                         private juce::ChangeListener
{
public:
    MainWindow (const juce::String& title, AppSettings& settings, AudioEngine& engine, PluginManager& plugins,
                InstanceGraph& instances, MidiRouter& router, ProjectController& project);
    ~MainWindow() override;

    void closeButtonPressed() override;

    // Writes the current size/position into the settings (does not flush to disk).
    void saveWindowState();

    void showAudioSettings();
    // Shows an audio device error in the status area (non-modal).
    void setAudioError (const juce::String& message);
    // Opens the plugin list window; startScan = also press "Escanear".
    void showPluginList (bool startScan = false);
    InstancePanel& getInstancePanel();
    // Audio > "Puertos MIDI…" (diagnostic window: port states and last messages).
    void showMidiPorts();
    MidiPortsWindow* getMidiPortsWindow() const noexcept { return midiPortsWindow.get(); }

    //==============================================================================
    // Project actions (same paths as the menu items; also used by the self-tests).
    void newProject();
    void openProjectWithDialog();
    void openProject (const juce::File& file); // asks about unsaved changes first
    void openRecent (int index);
    // done(true) once the project is on disk; done(false) if cancelled or failed.
    void saveProject (std::function<void (bool)> done = {});
    void saveProjectAs (std::function<void (bool)> done = {});
    // Close button / Archivo > Salir / systemRequestedQuit: asks, then quits.
    void requestQuit();
    // Runs `next` if there are no unsaved changes, or after the user chose "Guardar" (and the
    // save worked) or "No guardar". Nothing happens on "Cancelar".
    void confirmDiscardThen (std::function<void()> next);

    enum class SaveChoice
    {
        save,
        discard,
        cancel
    };

    // Self-tests: answers the "cambios sin guardar" question without showing it.
    std::function<SaveChoice()> savePromptOverride;
    // Self-tests: errors and load warnings go to onMessage instead of message boxes.
    bool suppressDialogs = false;
    // false (self-tests): do not remember the last project folder in settings.xml.
    bool rememberProjectFolder = true;
    std::function<void (const juce::String&)> onMessage;
    // Number of times the "cambios sin guardar" question was asked (shown or overridden).
    int getNumSavePrompts() const noexcept { return numSavePrompts; }

    juce::String getProjectInfoText() const;

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
        cmdAudioSettings,
        cmdMidiPorts,
        cmdNew,
        cmdOpen,
        cmdSave,
        cmdSaveAs,
        cmdClearRecent,
        cmdRecentBase = 100 // + index
    };

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    // ProjectController changes (name, dirty flag, progress).
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateTitle();

    void showAboutBox();
    void showMessage (const juce::String& title, const juce::String& message, bool isError);
    void openProjectNow (const juce::File& file);

    juce::String appName;
    AppSettings& settings;
    AudioEngine& engine;
    PluginManager& plugins;
    MidiRouter& router;
    ProjectController& project;

    std::unique_ptr<PluginListWindow> pluginListWindow;
    std::unique_ptr<MidiPortsWindow> midiPortsWindow;
    juce::Component::SafePointer<juce::DialogWindow> audioSettingsWindow;
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool savePromptShowing = false;
    int numSavePrompts = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

} // namespace vstplayer
