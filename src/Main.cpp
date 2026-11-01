#include <juce_gui_extra/juce_gui_extra.h>

#include "app/AppSettings.h"
#include "app/CommandLine.h"
#include "app/InstanceSelfTest.h"
#include "app/MainWindow.h"
#include "app/Translations.h"
#include "engine/AudioEngine.h"
#include "engine/InstanceGraph.h"
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
        const auto args = getCommandLineParameterArray();
        return cli::detectMode (args) != cli::Mode::gui || args.contains (kInstanceSelfTestFlag);
    }

    void initialise (const juce::String&) override
    {
        const auto args = getCommandLineParameterArray();
        const auto mode = cli::detectMode (args);

        if (args.contains ("--debug-to-stdout"))
            cli::installDebugOutputCapture();

        if (mode != cli::Mode::gui)
        {
            runCommandLineMode (mode, args);
            return;
        }

        settings = std::make_unique<AppSettings>();
        installSpanishTranslations();

        // The self-test changes the audio device temporarily: never persist that (nor the
        // window state).
        isSelfTest = args.contains (kInstanceSelfTestFlag);

        plugins = std::make_unique<PluginManager> (*settings);
        audioEngine = std::make_unique<AudioEngine> (*settings, ! isSelfTest);
        const auto audioError = audioEngine->initialise();
        instanceGraph = std::make_unique<InstanceGraph> (*audioEngine, *plugins);

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *settings, *audioEngine, *plugins, *instanceGraph);

        // GUI test hooks (used by the automated checks, harmless for users):
        //   --show-plugins, --gui-scan (open the plugin list and press "Escanear"),
        //   --show-audio-settings, --instance-selftest (see InstanceSelfTest.h)
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

        if (isSelfTest)
        {
            const auto pluginIndex = args.indexOf ("--plugin");
            const auto singlePlugin = pluginIndex >= 0 && pluginIndex + 1 < args.size() ? args[pluginIndex + 1].unquoted() : juce::String();
            selfTest = std::make_unique<InstanceSelfTest> (*mainWindow, *audioEngine, *plugins, *instanceGraph, singlePlugin);
            selfTest->start();
        }
    }

    void shutdown() override
    {
        // Every quit path (close button, Archivo > Salir, WM_CLOSE, logoff) ends here.
        selfTest = nullptr;

        if (mainWindow != nullptr && ! isSelfTest)
            mainWindow->saveWindowState();

        // Order matters: the window (and the plugin editors it owns) goes first, then the
        // instances, then the audio engine that owns the graph.
        mainWindow = nullptr;
        instanceGraph = nullptr;

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
    std::unique_ptr<InstanceGraph> instanceGraph;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<InstanceSelfTest> selfTest;
    bool isSelfTest = false;

    static constexpr const char* kInstanceSelfTestFlag = "--instance-selftest";
};

} // namespace vstplayer

START_JUCE_APPLICATION (vstplayer::VstPlayerApplication)
