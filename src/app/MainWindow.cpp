#include "app/MainWindow.h"

#include "app/AppSettings.h"
#include "engine/AudioEngine.h"
#include "engine/PluginManager.h"
#include "project/Project.h"
#include "project/ProjectController.h"
#include "ui/InstancePanel.h"
#include "ui/MainContent.h"
#include "ui/MidiPortsWindow.h"
#include "ui/PluginListWindow.h"

#include <juce_audio_utils/juce_audio_utils.h>

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace vstplayer
{

namespace
{
constexpr int kDefaultWidth = 1024;
constexpr int kDefaultHeight = 700;
constexpr int kMinWidth = 860;
constexpr int kMinHeight = 540;
constexpr const char* kLastProjectFolderKey = "lastProjectFolder";

// JUCE registers its window class with ExtractAssociatedIcon for both icons, so title bars show
// the large drawing shrunk. Give the class (shared by every JUCE window: main window, dialogs,
// plugin editors) the small icon size from the exe's .ico, i.e. the simplified 16 / 32 px drawing
// (resources/icon-small.svg). IDI_ICON1 is the resource name JUCE's generated .rc uses.
void useSmallIconForTitleBars (juce::ComponentPeer* peer)
{
    if (peer == nullptr)
        return;

    auto* icon = static_cast<HICON> (LoadImageW (GetModuleHandleW (nullptr), L"IDI_ICON1", IMAGE_ICON,
                                                 GetSystemMetrics (SM_CXSMICON), GetSystemMetrics (SM_CYSMICON),
                                                 LR_DEFAULTCOLOR | LR_SHARED));
    if (icon != nullptr)
        SetClassLongPtrW (static_cast<HWND> (peer->getNativeHandle()), GCLP_HICONSM, reinterpret_cast<LONG_PTR> (icon));
}
} // namespace

MainWindow::MainWindow (const juce::String& title, AppSettings& s, AudioEngine& e, PluginManager& p, InstanceGraph& instances,
                        MidiRouter& r, ProjectController& pc)
    : DocumentWindow (title,
                      juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons),
      appName (title),
      settings (s),
      engine (e),
      plugins (p),
      router (r),
      project (pc)
{
    setUsingNativeTitleBar (true);
    setMenuBar (this);

    auto content = std::make_unique<MainContent> (engine, plugins, instances, router);
    content->onShowAudioSettings = [this] { showAudioSettings(); };
    content->onShowPlugins = [this] { showPluginList(); };
    content->onShowMidiPorts = [this] { showMidiPorts(); };
    content->onNew = [this] { newProject(); };
    content->onOpen = [this] { openProjectWithDialog(); };
    content->onRecent = [this] { showRecentMenu(); };
    content->onSave = [this] { saveProject(); };
    content->onSaveAs = [this] { saveProjectAs(); };
    content->isScanning = [this] { return pluginListWindow != nullptr && pluginListWindow->isScanning(); };
    setContentOwned (content.release(), true);

    // Shortcuts: the key mappings listen at the window level, so they work wherever the focus is
    // inside it (keys a focused component uses itself, e.g. Esc while renaming, stay there).
    commandManager.registerAllCommandsForTarget (this);
    commandManager.setFirstCommandTarget (this);
    addKeyListener (commandManager.getKeyMappings());
    setApplicationCommandManagerToWatch (&commandManager);

    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, 10000, 10000);

    const auto savedState = settings.getMainWindowState();
    if (savedState.isEmpty() || ! restoreWindowStateFromString (savedState))
        centreWithSize (kDefaultWidth, kDefaultHeight);

    setVisible (true);
    useSmallIconForTitleBars (getPeer());

    project.openEditor = [this] (const juce::String& id) { return getInstancePanel().openEditor (id); };
    project.syncEditorWindows = [this] { getInstancePanel().syncEditorWindowStates(); };
    project.onLoadWarnings = [this] (const juce::StringArray& warnings)
    {
        showMessage (juce::String::fromUTF8 ("Proyecto abierto con advertencias"),
                     project.getProjectName() + ":\n\n- " + warnings.joinIntoString ("\n- "), false);
    };
    project.addChangeListener (this);
    updateTitle();
}

MainWindow::~MainWindow()
{
    removeKeyListener (commandManager.getKeyMappings());
    setApplicationCommandManagerToWatch (nullptr);
    commandManager.setFirstCommandTarget (nullptr);
    project.removeChangeListener (this);
    project.openEditor = nullptr;
    project.syncEditorWindows = nullptr;
    project.onLoadWarnings = nullptr;
    fileChooser = nullptr;

    if (audioSettingsWindow != nullptr)
        delete audioSettingsWindow.getComponent();

    pluginListWindow = nullptr;
    midiPortsWindow = nullptr;
    setMenuBar (nullptr);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

//==============================================================================
void MainWindow::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateTitle();
}

juce::String MainWindow::getProjectInfoText() const
{
    auto text = "Proyecto: " + project.getDisplayName();

    if (project.hasFile())
        text << "  (" << project.getProjectFile().getFullPathName() << ")";

    if (project.isLoading())
        text << juce::String::fromUTF8 ("  —  ") << project.describeProgress();

    return text;
}

void MainWindow::updateTitle()
{
    setName (appName + juce::String::fromUTF8 (" — ") + project.getDisplayName());

    if (auto* content = dynamic_cast<MainContent*> (getContentComponent()))
        content->setProjectInfo (project.getDisplayName(), project.hasFile() ? project.getProjectFile().getFullPathName() : juce::String(),
                                 project.isLoading() ? project.describeProgress() : juce::String());
}

void MainWindow::showMessage (const juce::String& title, const juce::String& message, bool isError)
{
    if (auto* content = dynamic_cast<MainContent*> (getContentComponent()))
        content->setStatus (title + ": " + message.replace ("\n", " "), isError);

    if (suppressDialogs)
    {
        if (onMessage)
            onMessage (title + ": " + message);

        return;
    }

    juce::AlertWindow::showMessageBoxAsync (isError ? juce::MessageBoxIconType::WarningIcon : juce::MessageBoxIconType::InfoIcon,
                                            title, message, "Aceptar", this);
}

void MainWindow::confirmDiscardThen (std::function<void()> next)
{
    if (! project.isDirty())
    {
        next();
        return;
    }

    if (savePromptShowing)
        return; // already asking (e.g. close button pressed twice)

    ++numSavePrompts;

    const auto handle = [safeThis = juce::Component::SafePointer<MainWindow> (this), next] (SaveChoice choice)
    {
        if (safeThis == nullptr)
            return;

        safeThis->savePromptShowing = false;

        switch (choice)
        {
            case SaveChoice::save:
                safeThis->saveProject ([next] (bool saved) { if (saved) next(); });
                break;

            case SaveChoice::discard:
                next();
                break;

            case SaveChoice::cancel:
                break;
        }
    };

    if (savePromptOverride)
    {
        handle (savePromptOverride());
        return;
    }

    savePromptShowing = true;
    juce::AlertWindow::showYesNoCancelBox (juce::MessageBoxIconType::QuestionIcon,
                                           "Cambios sin guardar",
                                           "El proyecto \"" + project.getProjectName()
                                               + juce::String::fromUTF8 ("\" tiene cambios sin guardar. ¿Guardarlos?"),
                                           "Guardar", "No guardar", "Cancelar", this,
                                           juce::ModalCallbackFunction::create ([handle] (int result)
                                           {
                                               handle (result == 1 ? SaveChoice::save : result == 2 ? SaveChoice::discard : SaveChoice::cancel);
                                           }));
}

void MainWindow::requestQuit()
{
    juce::Component::SafePointer<MainWindow> safeThis (this);

    confirmDiscardThen ([safeThis]
    {
        // Close the plugin list while the message loop still runs: its "Scan complete" box
        // (JUCE, shown when a file failed to scan) is deleted asynchronously when dismissed, and
        // after quit() that would never happen (leaked window at shutdown).
        if (safeThis != nullptr)
            safeThis->pluginListWindow = nullptr;

        juce::JUCEApplication::getInstance()->quit();
    });
}

void MainWindow::newProject()
{
    confirmDiscardThen ([this] { project.newProject(); });
}

void MainWindow::openProject (const juce::File& file)
{
    confirmDiscardThen ([safeThis = juce::Component::SafePointer<MainWindow> (this), file]
    {
        if (safeThis != nullptr)
            safeThis->openProjectNow (file);
    });
}

void MainWindow::openRecent (int index)
{
    const auto file = project.getRecentProjects().getFile (index);

    if (file != juce::File())
        openProject (file);
}

void MainWindow::openProjectNow (const juce::File& file)
{
    const auto result = project.open (file);

    if (result.failed())
    {
        showMessage ("No se pudo abrir el proyecto", result.getErrorMessage(), true);
        return;
    }

    getMainContent().setStatus ("Abierto: " + file.getFullPathName());

    if (rememberProjectFolder)
        settings.getProperties().setValue (kLastProjectFolderKey, file.getParentDirectory().getFullPathName());
}

void MainWindow::openProjectWithDialog()
{
    confirmDiscardThen ([safeThis = juce::Component::SafePointer<MainWindow> (this)]
    {
        if (safeThis == nullptr)
            return;

        auto& self = *safeThis;
        const auto folder = self.settings.getProperties().getValue (kLastProjectFolderKey);
        const auto start = folder.isNotEmpty() && juce::File (folder).isDirectory()
                               ? juce::File (folder)
                               : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

        self.chooseFile (false, start, [safeThis] (const juce::File& file)
        {
            if (safeThis != nullptr && file != juce::File())
                safeThis->openProjectNow (file);
        });
    });
}

void MainWindow::saveProject (std::function<void (bool)> done)
{
    if (! project.hasFile())
    {
        saveProjectAs (std::move (done));
        return;
    }

    const auto result = project.save();

    if (result.failed())
        showMessage ("No se pudo guardar el proyecto", result.getErrorMessage(), true);
    else
        getMainContent().setStatus ("Guardado: " + project.getProjectFile().getFullPathName());

    if (done)
        done (result.wasOk());
}

void MainWindow::saveProjectAs (std::function<void (bool)> done)
{
    const auto folder = settings.getProperties().getValue (kLastProjectFolderKey);
    auto start = project.hasFile() ? project.getProjectFile()
                                   : (folder.isNotEmpty() && juce::File (folder).isDirectory()
                                          ? juce::File (folder)
                                          : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory))
                                         .getChildFile (project.getProjectName() + project::kFileSuffix);

    chooseFile (true, start, [safeThis = juce::Component::SafePointer<MainWindow> (this), done] (const juce::File& file)
                              {
                                  if (safeThis == nullptr || file == juce::File())
                                  {
                                      if (done)
                                          done (false);

                                      return;
                                  }

                                  const auto result = safeThis->project.saveAs (file);

                                  if (result.failed())
                                      safeThis->showMessage ("No se pudo guardar el proyecto", result.getErrorMessage(), true);
                                  else
                                  {
                                      safeThis->getMainContent().setStatus ("Guardado: " + safeThis->project.getProjectFile().getFullPathName());

                                      if (safeThis->rememberProjectFolder)
                                          safeThis->settings.getProperties().setValue (
                                              kLastProjectFolderKey, safeThis->project.getProjectFile().getParentDirectory().getFullPathName());
                                  }

                                  if (done)
                                      done (result.wasOk());
                              });
}

void MainWindow::saveWindowState()
{
    settings.setMainWindowState (getWindowStateAsString());
}

InstancePanel& MainWindow::getInstancePanel()
{
    return getMainContent().getInstancePanel();
}

MainContent& MainWindow::getMainContent()
{
    return dynamic_cast<MainContent&> (*getContentComponent());
}

void MainWindow::chooseFile (bool forSave, const juce::File& start, std::function<void (const juce::File&)> chosen)
{
    if (fileChooserOverride)
    {
        // Self-tests: no native dialog. Answered asynchronously, like the real chooser.
        const auto file = fileChooserOverride (forSave);
        juce::MessageManager::callAsync ([chosen, file] { chosen (file); });
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser> (forSave ? "Guardar proyecto como" : "Abrir proyecto", start,
                                                       juce::String ("*") + project::kFileSuffix);
    const auto chooserFlags = forSave ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                     | juce::FileBrowserComponent::warnAboutOverwriting
                               : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (chooserFlags, [chosen] (const juce::FileChooser& chooser) { chosen (chooser.getResult()); });
}

juce::PopupMenu MainWindow::buildRecentMenu()
{
    juce::PopupMenu recentMenu;
    const auto& recent = project.getRecentProjects().getPaths();

    for (int i = 0; i < recent.size(); ++i)
        recentMenu.addItem (cmdRecentBase + i, juce::String (i + 1) + ". " + recent[i]);

    if (recent.isEmpty())
        recentMenu.addItem (cmdClearRecent, "(sin proyectos recientes)", false);
    else
    {
        recentMenu.addSeparator();
        recentMenu.addItem (cmdClearRecent, "Limpiar la lista");
    }

    return recentMenu;
}

void MainWindow::showRecentMenu()
{
    auto& button = getMainContent().getRecentButton();
    buildRecentMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&button),
                                     [safeThis = juce::Component::SafePointer<MainWindow> (this)] (int result)
                                     {
                                         if (safeThis != nullptr && result != 0)
                                             safeThis->menuItemSelected (result, fileMenu);
                                     });
}

//==============================================================================
void MainWindow::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.addArray ({ cmdNew, cmdOpen, cmdSave, cmdSaveAs, cmdPanic });
}

void MainWindow::getCommandInfo (juce::CommandID commandID, juce::ApplicationCommandInfo& result)
{
    const auto ctrl = juce::ModifierKeys::commandModifier;

    switch (commandID)
    {
        case cmdNew:
            result.setInfo ("Nuevo", "Proyecto nuevo", "Archivo", 0);
            result.addDefaultKeypress ('n', ctrl);
            break;

        case cmdOpen:
            result.setInfo (juce::String::fromUTF8 ("Abrir…"), "Abrir un proyecto", "Archivo", 0);
            result.addDefaultKeypress ('o', ctrl);
            break;

        case cmdSave:
            result.setInfo ("Guardar", "Guardar el proyecto", "Archivo", 0);
            result.addDefaultKeypress ('s', ctrl);
            break;

        case cmdSaveAs:
            result.setInfo (juce::String::fromUTF8 ("Guardar como…"), "Guardar el proyecto con otro nombre", "Archivo", 0);
            result.addDefaultKeypress ('s', ctrl | juce::ModifierKeys::shiftModifier);
            break;

        case cmdPanic:
            result.setInfo (juce::String::fromUTF8 ("Pánico"), "All notes off + all sound off", "Audio", 0);
            result.addDefaultKeypress (juce::KeyPress::escapeKey, juce::ModifierKeys::noModifiers);
            break;

        default:
            break;
    }
}

bool MainWindow::perform (const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case cmdNew:    newProject(); return true;
        case cmdOpen:   openProjectWithDialog(); return true;
        case cmdSave:   saveProject(); return true;
        case cmdSaveAs: saveProjectAs(); return true;
        case cmdPanic:  getInstancePanel().panic(); return true;
        default:        return false;
    }
}

void MainWindow::setAudioError (const juce::String& message)
{
    if (auto* content = dynamic_cast<MainContent*> (getContentComponent()))
        content->setAudioError (message);
}

void MainWindow::showAudioSettings()
{
    if (audioSettingsWindow != nullptr)
    {
        audioSettingsWindow->toFront (true);
        return;
    }

    // 0 inputs, exactly 2 outputs (stereo pair), no MIDI here (MIDI inputs: Audio > Puertos MIDI and the instance rows).
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (engine.getDeviceManager(),
                                                                          0, 0, 2, 2,
                                                                          false, false, true, false);
    selector->setSize (520, 360);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = juce::String::fromUTF8 ("Configuración de audio");
    options.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    audioSettingsWindow = options.launchAsync();
}

void MainWindow::showMidiPorts()
{
    if (midiPortsWindow == nullptr)
    {
        midiPortsWindow = std::make_unique<MidiPortsWindow> (router);
        midiPortsWindow->centreAroundComponent (this, midiPortsWindow->getWidth(), midiPortsWindow->getHeight());
    }

    midiPortsWindow->setVisible (true);
    midiPortsWindow->toFront (true);
}

void MainWindow::showPluginList (bool startScan)
{
    if (pluginListWindow == nullptr)
        pluginListWindow = std::make_unique<PluginListWindow> (plugins, settings);

    pluginListWindow->setVisible (true);
    pluginListWindow->toFront (true);

    if (startScan)
        pluginListWindow->startScan();
}

juce::StringArray MainWindow::getMenuBarNames()
{
    return { "Archivo", "Audio", "Ayuda" };
}

juce::PopupMenu MainWindow::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    switch (topLevelMenuIndex)
    {
        case fileMenu:
        {
            menu.addCommandItem (&commandManager, cmdNew);
            menu.addCommandItem (&commandManager, cmdOpen);
            menu.addCommandItem (&commandManager, cmdSave);
            menu.addCommandItem (&commandManager, cmdSaveAs);
            menu.addSubMenu ("Recientes", buildRecentMenu());
            menu.addSeparator();
            menu.addItem (cmdPlugins, juce::String::fromUTF8 ("Plugins…"));
            menu.addSeparator();
            menu.addItem (cmdQuit, juce::String::fromUTF8 ("Salir"));
            break;
        }

        case audioMenu:
            menu.addItem (cmdAudioSettings, juce::String::fromUTF8 ("Configuración de audio…"));
            menu.addItem (cmdMidiPorts, juce::String::fromUTF8 ("Puertos MIDI…"));
            menu.addSeparator();
            menu.addCommandItem (&commandManager, cmdPanic);
            break;

        case helpMenu:
            menu.addItem (cmdAbout, juce::String::fromUTF8 ("Acerca de VstPlayer…"));
            break;

        default:
            break;
    }

    return menu;
}

void MainWindow::menuItemSelected (int menuItemID, int)
{
    switch (menuItemID)
    {
        case cmdQuit:
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            break;

        case cmdNew:
            newProject();
            break;

        case cmdOpen:
            openProjectWithDialog();
            break;

        case cmdSave:
            saveProject();
            break;

        case cmdSaveAs:
            saveProjectAs();
            break;

        case cmdClearRecent:
            project.getRecentProjects().clear();
            break;

        case cmdPlugins:
            showPluginList();
            break;

        case cmdAudioSettings:
            showAudioSettings();
            break;

        case cmdMidiPorts:
            showMidiPorts();
            break;

        case cmdAbout:
            showAboutBox();
            break;

        default:
            if (menuItemID >= cmdRecentBase && menuItemID < cmdRecentBase + 100)
                openRecent (menuItemID - cmdRecentBase);
            break;
    }
}

void MainWindow::showAboutBox()
{
    const auto message = "VstPlayer " + juce::JUCEApplication::getInstance()->getApplicationVersion()
                         + "\n" + juce::String::fromUTF8 ("Host de instrumentos VST3.")
                         + "\n" + juce::SystemStats::getJUCEVersion();

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                            "Acerca de VstPlayer",
                                            message,
                                            "Aceptar",
                                            this);
}

} // namespace vstplayer
