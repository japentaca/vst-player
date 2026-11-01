#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <vector>

namespace vstplayer
{

class AudioEngine;
class InstanceGraph;
class MainWindow;
class MidiRouter;
class PluginManager;
class ProjectController;

// GUI test hook "--instance-selftest": drives the real main window without user input and
// quits with exit code 0 (all checks passed) or 1. Output goes to stdout (cli::print).
//   1. Loads Dexed and Surge XT (async, through InstancePanel::addInstance), at -30 dB.
//   2. Opens and closes both editors; reopens Dexed's at the saved position.
//   3. Plays notes with the virtual keyboard state (Dexed and Surge selected in turn) and
//      checks a non-zero signal at each gain node and at the output mixer; mute check; panic.
//   4. Changes the audio buffer size with the instances loaded (another size of the same
//      device type, or DirectSound if the type only offers one), plays again, restores.
//   5. Loads sforzando, opens and closes its editor, removes it.
//   6. Removes Surge XT with its editor open; quits with Dexed's editor still open.
// "--instance-selftest --plugin <name>": short run for one plugin only (load, editor open and
// close, note on/off, remove), handy to find which plugin causes a problem.
// "--midi-selftest" (stage 4, see MidiSelfTest.cpp): MIDI input ports and routing. Sends MIDI
// from child processes ("VstPlayer.exe --midi-send") to the loopMIDI port and checks per
// instance and per channel what reached each plugin (keep, remapTo, unrouted channels, one
// channel to two instances, retrigger, notes held across a route change, panic, system
// messages), the "Puertos MIDI" window and the latency the AudioDeviceManager reports with
// DirectSound 256 and 128. "--midi-selftest --restart-loopmidi" also terminates loopMIDI.exe
// and starts it again (minimized) to check that a port deleted and re-created while in use
// is picked up again by itself.
// The audio setup is not persisted in this mode (see Main.cpp).
class InstanceSelfTest final : private juce::Timer
{
public:
    enum class Kind
    {
        instances, // --instance-selftest (stage 3)
        midi,      // --midi-selftest (stage 4)
        project    // --project-selftest <phase> (stage 5, see ProjectSelfTest.cpp)
    };

    InstanceSelfTest (MainWindow& window, AudioEngine& engine, PluginManager& plugins, InstanceGraph& graph,
                      MidiRouter& router, Kind kind = Kind::instances, juce::String singlePluginName = {});
    ~InstanceSelfTest() override;

    void start();

    // --project-selftest: phase = save | verify-cli | verify-recent | extras; testDir holds the
    // project files and the expected fingerprint; commandLineProject = path given on the command line.
    void setProjectTest (ProjectController& controller, const juce::String& phase, const juce::File& testDir,
                         const juce::File& commandLineProject);

private:
    struct Step
    {
        juce::String name;
        std::function<bool()> poll; // true = done; called every tick until done or timeout
        double timeoutSeconds = 5.0;
    };

    void timerCallback() override;
    void buildSteps();
    void buildSinglePluginSteps();
    void buildMidiSteps(); // MidiSelfTest.cpp
    void buildProjectSteps(); // ProjectSelfTest.cpp
    static void clearProjectTestState(); // ProjectSelfTest.cpp (its vars must go before JUCE's leak detector runs)
    void finish (bool ok);

    // Step helpers.
    void action (const juce::String& name, std::function<bool()> fn); // fn returns success
    void waitMs (int ms);
    void waitFor (const juce::String& name, std::function<bool()> condition, double timeoutSeconds);
    bool check (bool condition, const juce::String& what);

    MainWindow& window;
    AudioEngine& engine;
    PluginManager& plugins;
    InstanceGraph& graph;
    MidiRouter& router;
    const Kind kind;
    const juce::String singlePluginName;

    ProjectController* project = nullptr;
    juce::String projectPhase;
    juce::File projectTestDir;
    juce::File projectFromCommandLine;

    std::vector<Step> steps;
    size_t current = 0;
    double stepStartMs = 0.0;
    bool stepStarted = false;
    int failures = 0;
    bool finished = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstanceSelfTest)
};

} // namespace vstplayer
