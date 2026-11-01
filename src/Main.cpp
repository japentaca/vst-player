#include <juce_gui_extra/juce_gui_extra.h>

#include "app/AppSettings.h"
#include "app/CommandLine.h"
#include "app/InstanceSelfTest.h"
#include "app/MainWindow.h"
#include "app/Translations.h"
#include "engine/AudioEngine.h"
#include "engine/InstanceGraph.h"
#include "engine/MidiRouter.h"
#include "engine/PluginManager.h"
#include "project/ProjectController.h"
#include "ui/InstancePanel.h"
#include "ui/Theme.h"

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
        return cli::detectMode (args) != cli::Mode::gui || args.contains (kInstanceSelfTestFlag)
               || args.contains (kMidiSelfTestFlag) || args.contains (kProjectSelfTestFlag) || args.contains (kUiSelfTestFlag);
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
        isSelfTest = args.contains (kInstanceSelfTestFlag) || args.contains (kMidiSelfTestFlag)
                     || args.contains (kProjectSelfTestFlag) || args.contains (kUiSelfTestFlag);

        // Dark theme for every window (main window, dialogs, plugin list, MIDI ports).
        lookAndFeel = std::make_unique<DarkLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        plugins = std::make_unique<PluginManager> (*settings);
        audioEngine = std::make_unique<AudioEngine> (*settings, ! isSelfTest);
        const auto audioError = audioEngine->initialise();
        instanceGraph = std::make_unique<InstanceGraph> (*audioEngine, *plugins);
        midiRouter = std::make_unique<MidiRouter> (*instanceGraph);

        // recent.json and the autosave: %APPDATA%\VstPlayer, or --project-data-dir <dir>. The
        // self-tests never touch the user's files: without --project-data-dir they keep the
        // recent list in memory and do not autosave.
        auto dataDir = isSelfTest ? juce::File() : AppSettings::getUserDataDirectory();
        if (const auto dirArg = valueOf (args, "--project-data-dir"); dirArg.isNotEmpty())
            dataDir = juce::File::getCurrentWorkingDirectory().getChildFile (dirArg);

        projectController = std::make_unique<ProjectController> (*audioEngine, *plugins, *instanceGraph, *midiRouter, dataDir);

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *settings, *audioEngine, *plugins, *instanceGraph,
                                                   *midiRouter, *projectController);

        if (! isSelfTest)
        {
            // Virtual keyboard folded or not, as the user left it.
            auto& panel = mainWindow->getInstancePanel();
            panel.setKeyboardVisible (settings->getProperties().getBoolValue (kKeyboardVisibleKey, true));
            panel.onKeyboardVisibilityChanged = [this] (bool visible) { settings->getProperties().setValue (kKeyboardVisibleKey, visible); };
        }

        if (isSelfTest)
        {
            // Nobody is watching: no modal questions or message boxes.
            mainWindow->suppressDialogs = true;
            mainWindow->rememberProjectFolder = false;
            mainWindow->onMessage = [] (const juce::String& text) { cli::print ("  [mensaje] " + text); };
            mainWindow->savePromptOverride = [] { return MainWindow::SaveChoice::discard; };
        }

        // GUI test hooks (used by the automated checks, harmless for users):
        //   --show-plugins, --gui-scan (open the plugin list and press "Escanear"),
        //   --show-audio-settings, --show-midi-ports, --instance-selftest, --midi-selftest
        //   (see InstanceSelfTest.h)
        if (args.contains ("--show-audio-settings"))
            mainWindow->showAudioSettings();

        if (args.contains ("--show-midi-ports"))
            mainWindow->showMidiPorts();

        if (args.contains ("--show-plugins") || args.contains ("--gui-scan"))
            mainWindow->showPluginList (args.contains ("--gui-scan"));

        if (audioError.isNotEmpty())
        {
            // Non-modal: shown in the main window status line (and on stdout if redirected).
            mainWindow->setAudioError (audioError);
            cli::print ("Audio error: " + audioError);
        }

        // "VstPlayer.exe ruta\proyecto.vstproj.json" opens that project.
        if (const auto projectFile = findProjectArgument (args); projectFile != juce::File())
            mainWindow->openProject (projectFile);

        if (isSelfTest)
        {
            const auto pluginIndex = args.indexOf ("--plugin");
            const auto singlePlugin = pluginIndex >= 0 && pluginIndex + 1 < args.size() ? args[pluginIndex + 1].unquoted() : juce::String();
            const auto kind = args.contains (kUiSelfTestFlag)       ? InstanceSelfTest::Kind::ui
                              : args.contains (kProjectSelfTestFlag) ? InstanceSelfTest::Kind::project
                              : args.contains (kMidiSelfTestFlag)   ? InstanceSelfTest::Kind::midi
                                                                     : InstanceSelfTest::Kind::instances;
            selfTest = std::make_unique<InstanceSelfTest> (*mainWindow, *audioEngine, *plugins, *instanceGraph, *midiRouter,
                                                           kind, singlePlugin);

            if (kind == InstanceSelfTest::Kind::project || kind == InstanceSelfTest::Kind::ui)
                selfTest->setProjectTest (*projectController, valueOf (args, kProjectSelfTestFlag),
                                          juce::File::getCurrentWorkingDirectory().getChildFile (valueOf (args, "--project-test-dir")),
                                          findProjectArgument (args));
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
        // MIDI router (stops writing to the instances), then the instances, then the audio
        // engine that owns the graph.
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel = nullptr;
        projectController = nullptr;
        midiRouter = nullptr;
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

    void systemRequestedQuit() override
    {
        // Asks about unsaved changes first (Guardar / No guardar / Cancelar).
        if (mainWindow != nullptr)
            mainWindow->requestQuit();
        else
            quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (mainWindow == nullptr)
            return;

        mainWindow->toFront (true);

        // "VstPlayer.exe proyecto.vstproj.json" while the app is already open: open it here.
        if (const auto file = findProjectArgument (juce::StringArray::fromTokens (commandLine, true)); file != juce::File())
            mainWindow->openProject (file);
    }

private:
    static juce::String valueOf (const juce::StringArray& args, const juce::String& flag)
    {
        const auto index = args.indexOf (flag);
        return index >= 0 && index + 1 < args.size() ? args[index + 1].unquoted() : juce::String();
    }

    // First argument that is not an option (nor an option's value) and ends with ".json".
    static juce::File findProjectArgument (const juce::StringArray& args)
    {
        static const juce::StringArray optionsWithValue { "--plugin", "--project-data-dir", "--project-test-dir", kProjectSelfTestFlag,
                                                          "--type" };

        for (int i = 0; i < args.size(); ++i)
        {
            const auto arg = args[i].unquoted().trim();

            if (arg.isEmpty() || arg.startsWith ("-") || (i > 0 && optionsWithValue.contains (args[i - 1])))
                continue;

            if (arg.endsWithIgnoreCase (".json"))
                return juce::File::getCurrentWorkingDirectory().getChildFile (arg);
        }

        return {};
    }

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

            case cli::Mode::midiSend:
                exitCode = cli::runMidiSend (args);
                break;

            case cli::Mode::midiPorts:
                exitCode = cli::runMidiPorts();
                break;

            case cli::Mode::midiMonitor:
                exitCode = cli::runMidiMonitor (args);
                break;

            case cli::Mode::gui:
            default:
                break;
        }

        setApplicationReturnValue (exitCode);
        quit();
    }

    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<DarkLookAndFeel> lookAndFeel;
    std::unique_ptr<PluginManager> plugins;
    std::unique_ptr<AudioEngine> audioEngine;
    std::unique_ptr<InstanceGraph> instanceGraph;
    std::unique_ptr<MidiRouter> midiRouter;
    std::unique_ptr<ProjectController> projectController;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<InstanceSelfTest> selfTest;
    bool isSelfTest = false;

    static constexpr const char* kInstanceSelfTestFlag = "--instance-selftest";
    static constexpr const char* kMidiSelfTestFlag = "--midi-selftest";
    static constexpr const char* kProjectSelfTestFlag = "--project-selftest";
    static constexpr const char* kUiSelfTestFlag = "--ui-selftest";
    static constexpr const char* kKeyboardVisibleKey = "keyboardVisible";
};

} // namespace vstplayer

START_JUCE_APPLICATION (vstplayer::VstPlayerApplication)
