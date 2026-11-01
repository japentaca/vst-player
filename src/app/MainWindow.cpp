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

namespace vstplayer
{

namespace
{
constexpr int kDefaultWidth = 1024;
constexpr int kDefaultHeight = 700;
constexpr int kMinWidth = 640;
constexpr int kMinHeight = 480;
constexpr const char* kLastProjectFolderKey = "lastProjectFolder";
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
    setContentOwned (content.release(), true);

    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, 10000, 10000);

    const auto savedState = settings.getMainWindowState();
    if (savedState.isEmpty() || ! restoreWindowStateFromString (savedState))
        centreWithSize (kDefaultWidth, kDefaultHeight);

    setVisible (true);

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
        content->setProjectInfo (getProjectInfoText());
}

void MainWindow::showMessage (const juce::String& title, const juce::String& message, bool isError)
{
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
    confirmDiscardThen ([] { juce::JUCEApplication::getInstance()->quit(); });
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
        showMessage ("No se pudo abrir el proyecto", result.getErrorMessage(), true);
    else
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

        self.fileChooser = std::make_unique<juce::FileChooser> ("Abrir proyecto", start, juce::String ("*") + project::kFileSuffix);
        self.fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                       [safeThis] (const juce::FileChooser& chooser)
                                       {
                                           const auto file = chooser.getResult();

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

    fileChooser = std::make_unique<juce::FileChooser> ("Guardar proyecto como", start, juce::String ("*") + project::kFileSuffix);
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safeThis = juce::Component::SafePointer<MainWindow> (this), done] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();

                                  if (safeThis == nullptr || file == juce::File())
                                  {
                                      if (done)
                                          done (false);

                                      return;
                                  }

                                  const auto result = safeThis->project.saveAs (file);

                                  if (result.failed())
                                      safeThis->showMessage ("No se pudo guardar el proyecto", result.getErrorMessage(), true);
                                  else if (safeThis->rememberProjectFolder)
                                      safeThis->settings.getProperties().setValue (kLastProjectFolderKey,
                                                                                   safeThis->project.getProjectFile().getParentDirectory().getFullPathName());

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
    return dynamic_cast<MainContent&> (*getContentComponent()).getInstancePanel();
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
            menu.addItem (cmdNew, "Nuevo");
            menu.addItem (cmdOpen, juce::String::fromUTF8 ("Abrir…"));
            menu.addItem (cmdSave, "Guardar");
            menu.addItem (cmdSaveAs, juce::String::fromUTF8 ("Guardar como…"));

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

            menu.addSubMenu ("Recientes", recentMenu);
            menu.addSeparator();
            menu.addItem (cmdPlugins, juce::String::fromUTF8 ("Plugins…"));
            menu.addSeparator();
            menu.addItem (cmdQuit, juce::String::fromUTF8 ("Salir"));
            break;
        }

        case audioMenu:
            menu.addItem (cmdAudioSettings, juce::String::fromUTF8 ("Configuración de audio…"));
            menu.addItem (cmdMidiPorts, juce::String::fromUTF8 ("Puertos MIDI…"));
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
