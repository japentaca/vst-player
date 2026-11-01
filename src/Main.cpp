#include <juce_gui_extra/juce_gui_extra.h>

#include "app/AppSettings.h"
#include "app/CommandLine.h"
#include "app/MainWindow.h"
#include "app/Translations.h"
#include "engine/AudioEngine.h"
#include "engine/PluginManager.h"

namespace vstplayer
{

class VstPlayerApplication final : public juce::JUCEApplication
{
public:
    VstPlayerApplication() = default;

    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    // Command-line modes (scanner child, --scan, self-tests) must run even while the GUI is open.
    bool moreThanOneInstanceAllowed() override
    {
        return cli::detectMode (getCommandLineParameterArray()) != cli::Mode::gui;
    }

    void initialise (const juce::String&) override
    {
        const auto args = getCommandLineParameterArray();
        const auto mode = cli::detectMode (args);

        if (mode != cli::Mode::gui)
        {
            runCommandLineMode (mode, args);
            return;
        }

        settings = std::make_unique<AppSettings>();
        installSpanishTranslations();

        plugins = std::make_unique<PluginManager> (*settings);
        audioEngine = std::make_unique<AudioEngine> (*settings);
        const auto audioError = audioEngine->initialise();

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *settings, *audioEngine, *plugins);

        // GUI test hooks (used by the automated checks, harmless for users):
        //   --show-plugins, --gui-scan (open the plugin list and press "Escanear"),
        //   --show-audio-settings
        if (args.contains ("--show-audio-settings"))
            mainWindow->showAudioSettings();

        if (args.contains ("--show-plugins") || args.contains ("--gui-scan"))
            mainWindow->showPluginList (args.contains ("--gui-scan"));

        if (audioError.isNotEmpty())
        {
            // Non-modal: shown in the main window status line (and on stdout if redirected).
            mainWindow->setAudioError (audioError);
            cli::print ("Audio error: " + audioError);
        }
    }

    void shutdown() override
    {
        // Every quit path (close button, Archivo > Salir, WM_CLOSE, logoff) ends here.
        if (mainWindow != nullptr)
            mainWindow->saveWindowState();

        mainWindow = nullptr;

        if (audioEngine != nullptr)
            audioEngine->saveDeviceState();

        audioEngine = nullptr;
        plugins = nullptr;

        if (settings != nullptr)
            settings->flush();

        settings = nullptr;
        juce::LocalisedStrings::setCurrentMappings (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr)
            mainWindow->toFront (true);
    }

private:
    void runCommandLineMode (cli::Mode mode, const juce::StringArray& args)
    {
        int exitCode = 0;

        switch (mode)
        {
            case cli::Mode::scanChild:
                exitCode = cli::runScanChild (args);
                break;

            case cli::Mode::scan:
            {
                AppSettings cliSettings;
                exitCode = cli::runScan (args, cliSettings);
                break;
            }

            case cli::Mode::audioSelfTest:
            {
                AppSettings cliSettings;
                exitCode = cli::runAudioSelfTest (args, cliSettings);
                break;
            }

            case cli::Mode::audioDevices:
                exitCode = cli::runAudioDevices();
                break;

            case cli::Mode::gui:
            default:
                break;
        }

        setApplicationReturnValue (exitCode);
        quit();
    }

    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<PluginManager> plugins;
    std::unique_ptr<AudioEngine> audioEngine;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace vstplayer

START_JUCE_APPLICATION (vstplayer::VstPlayerApplication)
