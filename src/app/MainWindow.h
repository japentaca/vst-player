#pragma once

#include "app/RecordingSettings.h"
#include "engine/Recorder.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

namespace vstplayer
{

class AppSettings;
class AudioEngine;
class InstanceGraph;
class InstancePanel;
class MainContent;
class MidiPortsWindow;
class MidiRouter;
class PluginManager;
class PluginListWindow;
class ProjectController;

// Main application window: menu bar (Archivo, Audio, Ayuda) and MainContent (top bar, instance
// rows, keyboard, status bar). Archivo: Nuevo, Abrir…, Guardar, Guardar como…, Recientes,
// Plugins…, Salir; the same actions are buttons of the top bar. Every action that would drop the
// current project (Nuevo, Abrir, Recientes, Salir, close button) first asks about unsaved
// changes. Title: "VstPlayer — <proyecto>" with " *" when modified.
// Keyboard shortcuts (ApplicationCommandManager, shown in the menus): Ctrl+N, Ctrl+O, Ctrl+S,
// Ctrl+Shift+S, Ctrl+R = grabar / detener, Esc = pánico.
// Recording (Audio menu and the "● Grabar" button): options from settings.xml (RecordingSettings),
// status bar messages when a recording starts / ends (also when the API or a device change did
// it); quitting while recording asks first. They work wherever the focus is inside the main window (a text
// being edited keeps its own Esc).
class MainWindow final : public juce::DocumentWindow,
                         public juce::ApplicationCommandTarget,
                         private juce::MenuBarModel,
                         private juce::ChangeListener,
                         private Recorder::Listener
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
    PluginListWindow* getPluginListWindow() const noexcept { return pluginListWindow.get(); }
    InstancePanel& getInstancePanel();
    MainContent& getMainContent();
    juce::ApplicationCommandManager& getCommandManager() noexcept { return commandManager; }
    // Shows the "Recientes" popup under the top bar button.
    void showRecentMenu();
    // Audio > "Puertos MIDI…" (diagnostic window: port states and last messages).
    void showMidiPorts();
    MidiPortsWindow* getMidiPortsWindow() const noexcept { return midiPortsWindow.get(); }

    // Recording: starts with the options of settings.xml, or stops. Messages in the status bar.
    void toggleRecording();
    juce::Result startRecording();
    void stopRecording();
    // Audio > "Opciones de grabación…".
    void showRecordingSettings();
    juce::DialogWindow* getRecordingSettingsWindow() const noexcept { return recordingSettingsWindow.getComponent(); }
    // Folder the next recording goes to (Audio > "Abrir carpeta de grabaciones").
    juce::File getRecordingFolder() const;
    // settings.xml, or recordingSettingsOverride (self-tests: never the user's folder).
    RecordingSettings getRecordingSettings() const;
    std::function<RecordingSettings()> recordingSettingsOverride;

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
    // Self-tests: replaces the native file chooser (Abrir / Guardar como). Returns the chosen
    // file, or juce::File() for "cancel". forSave = Guardar como.
    std::function<juce::File (bool forSave)> fileChooserOverride;
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
        cmdPanic,
        cmdRecord,
        cmdRecordingSettings,
        cmdOpenRecordingFolder,
        cmdRecentBase = 100 // + index
    };

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo (juce::CommandID commandID, juce::ApplicationCommandInfo& result) override;
    bool perform (const InvocationInfo& info) override;

    void chooseFile (bool forSave, const juce::File& start, std::function<void (const juce::File&)> chosen);
    juce::PopupMenu buildRecentMenu();

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    // ProjectController changes (name, dirty flag, progress).
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateTitle();

    // Recorder::Listener
    void recordingStarted (const Recorder::Info&) override;
    void recordingStopped (const Recorder::Result&) override;

    void showAboutBox();
    void showMessage (const juce::String& title, const juce::String& message, bool isError);
    void openProjectNow (const juce::File& file);

    juce::String appName;
    AppSettings& settings;
    AudioEngine& engine;
    PluginManager& plugins;
    MidiRouter& router;
    ProjectController& project;

    juce::ApplicationCommandManager commandManager;
    std::unique_ptr<PluginListWindow> pluginListWindow;
    std::unique_ptr<MidiPortsWindow> midiPortsWindow;
    juce::Component::SafePointer<juce::DialogWindow> audioSettingsWindow;
    juce::Component::SafePointer<juce::DialogWindow> recordingSettingsWindow;
    std::unique_ptr<juce::FileChooser> fileChooser;
    bool savePromptShowing = false;
    bool recordQuitPromptShowing = false;
    int numSavePrompts = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

} // namespace vstplayer
