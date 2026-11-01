#include "app/MainWindow.h"

#include "app/AppSettings.h"
#include "engine/AudioEngine.h"
#include "engine/PluginManager.h"
#include "ui/MainContent.h"
#include "ui/PluginListWindow.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace vstplayer
{

namespace
{
constexpr int kDefaultWidth = 1024;
constexpr int kDefaultHeight = 640;
constexpr int kMinWidth = 640;
constexpr int kMinHeight = 400;
} // namespace

MainWindow::MainWindow (const juce::String& title, AppSettings& s, AudioEngine& e, PluginManager& p)
    : DocumentWindow (title,
                      juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons),
      settings (s),
      engine (e),
      plugins (p)
{
    setUsingNativeTitleBar (true);
    setMenuBar (this);

    auto content = std::make_unique<MainContent> (engine, plugins);
    content->onShowAudioSettings = [this] { showAudioSettings(); };
    content->onShowPlugins = [this] { showPluginList(); };
    setContentOwned (content.release(), true);

    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, 10000, 10000);

    const auto savedState = settings.getMainWindowState();
    if (savedState.isEmpty() || ! restoreWindowStateFromString (savedState))
        centreWithSize (kDefaultWidth, kDefaultHeight);

    setVisible (true);
}

MainWindow::~MainWindow()
{
    if (audioSettingsWindow != nullptr)
        delete audioSettingsWindow.getComponent();

    pluginListWindow = nullptr;
    setMenuBar (nullptr);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void MainWindow::saveWindowState()
{
    settings.setMainWindowState (getWindowStateAsString());
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

    // 0 inputs, exactly 2 outputs (stereo pair), no MIDI here (MIDI ports arrive in stage 4).
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
            menu.addItem (cmdPlugins, juce::String::fromUTF8 ("Plugins…"));
            menu.addSeparator();
            menu.addItem (cmdQuit, juce::String::fromUTF8 ("Salir"));
            break;

        case audioMenu:
            menu.addItem (cmdAudioSettings, juce::String::fromUTF8 ("Configuración de audio…"));
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

        case cmdPlugins:
            showPluginList();
            break;

        case cmdAudioSettings:
            showAudioSettings();
            break;

        case cmdAbout:
            showAboutBox();
            break;

        default:
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
