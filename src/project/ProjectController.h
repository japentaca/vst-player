#pragma once

#include "engine/InstanceGraph.h"
#include "project/Project.h"
#include "project/RecentProjects.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <map>
#include <set>

namespace vstplayer
{

class AudioEngine;
class MidiRouter;
class PluginManager;

// Connects a project::Project with the running engine:
// - apply(): audio device, instances (loaded asynchronously; the saved state and the `params`
//   overrides are restored before each plugin enters the graph), MIDI routes, editor windows
//   (reopened at their saved position once the plugin is ready).
// - capture(): the current setup as a Project (getStateInformation of every plugin, routes,
//   gains, windows). Data the engine does not use is carried over from the loaded file:
//   unknown JSON properties, `params` (updated with the current values) and, for instances
//   whose plugin is missing or failed to load, the whole instance (state included).
// - Plugin resolution: KnownPluginList by uniqueId, then by file path, then by name. Not found
//   = "missing" instance (red row, kept as is in the file).
// - Unsaved changes: graph / router edit counters + plugin parameter / state notifications.
// - Recent projects (recent.json) and a backup autosave every 2 minutes while there are
//   unsaved changes (autosave.vstproj.json), both in the user data folder.
// No dialogs here: the UI (MainWindow) asks the user and reports errors.
// Message thread only.
class ProjectController final : public juce::ChangeBroadcaster, // name, dirty flag, loading progress changed
                                private InstanceGraph::Listener,
                                private juce::AudioProcessorListener,
                                private juce::Timer
{
public:
    // userDataDirectory: where recent.json and autosave.vstproj.json go. An invalid File
    // (juce::File()) keeps the recent list in memory only and disables the autosave.
    ProjectController (AudioEngine& engine, PluginManager& plugins, InstanceGraph& graph, MidiRouter& router,
                       const juce::File& userDataDirectory);
    ~ProjectController() override;

    //==============================================================================
    // Hooks for the UI (InstancePanel owns the editor windows).
    std::function<bool (const InstanceId&)> openEditor;
    std::function<void()> syncEditorWindows;
    // Called once a project finished loading (every plugin ready or failed) with the
    // problems found (missing plugins, audio device, plugins that failed...). Not called if
    // there were none.
    std::function<void (const juce::StringArray& warnings)> onLoadWarnings;

    //==============================================================================
    juce::String getProjectName() const { return projectName; }
    juce::File getProjectFile() const { return projectFile; }
    bool hasFile() const { return projectFile != juce::File(); }
    bool isDirty() const;
    void markDirty();
    // "Sin título", "Piano y pads", "Piano y pads *".
    juce::String getDisplayName() const;

    bool isLoading() const noexcept { return ! pendingLoads.empty(); }
    int getNumPendingLoads() const noexcept { return (int) pendingLoads.size(); }
    int getNumLoadsRequested() const noexcept { return loadsRequested; }
    // "Cargando proyecto: 2 de 3 instancias…" or empty.
    juce::String describeProgress() const;
    // Problems of the last apply() (complete once isLoading() is false).
    const juce::StringArray& getLastWarnings() const noexcept { return lastWarnings; }
    // Incremented every time a project (or "Nuevo") finished loading: every plugin ready or failed.
    juce::uint64 getLoadSerial() const noexcept { return loadSerial; }

    //==============================================================================
    project::Project capture();

    juce::Result save();                          // to getProjectFile()
    juce::Result saveAs (const juce::File& file); // adds ".vstproj.json" if missing
    // Reads the file and applies it. On a read error the current project is left untouched.
    juce::Result open (const juce::File& file);
    // Replaces the current setup. `file` may be juce::File() (untitled).
    void apply (const project::Project& project, const juce::File& file, juce::StringArray warnings = {});
    void newProject();

    //==============================================================================
    RecentProjects& getRecentProjects() noexcept { return recent; }

    juce::File getAutosaveFile() const;
    void setAutosaveIntervalSeconds (int seconds); // default 120
    juce::Result writeAutosave();
    int getNumAutosaves() const noexcept { return numAutosaves; }

    static constexpr int kDefaultAutosaveSeconds = 120;
    static constexpr const char* kAutosaveFileName = "autosave.vstproj.json";

    // Plugin resolution (public for tests). Empty optional = not found.
    std::optional<juce::PluginDescription> resolvePlugin (const project::PluginRef& ref) const;
    static project::PluginRef makePluginRef (const juce::PluginDescription& description);

private:
    // InstanceGraph::Listener
    void instanceListChanged() override;
    void instanceAboutToBeRemoved (const Instance&) override;
    void instanceLoadFinished (const Instance&) override;

    // AudioProcessorListener (any thread)
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override;
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override;
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override {}
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override {}
    void notePluginChange();

    void timerCallback() override;

    void markClean();
    void finishLoading();
    void detachAllPlugins();
    void restoreState (juce::AudioPluginInstance& plugin, const project::InstanceData& data, juce::StringArray& problems) const;

    AudioEngine& engine;
    PluginManager& plugins;
    InstanceGraph& graph;
    MidiRouter& router;
    const juce::File dataDirectory;
    RecentProjects recent;

    juce::String projectName;
    juce::File projectFile;

    // Carried over from the loaded file (see class comment).
    int loadedVersion = project::kCurrentVersion;
    juce::NamedValueSet projectExtra;
    project::AudioSetup loadedAudio;
    std::map<InstanceId, project::InstanceData> carried;
    std::vector<project::MidiRouteData> loadedRoutes;

    // Loading.
    std::set<InstanceId> pendingLoads;
    std::set<InstanceId> openEditorAfterLoad;
    int loadsRequested = 0;
    juce::uint64 loadSerial = 0;
    juce::StringArray lastWarnings;

    // Dirty tracking.
    bool explicitDirty = false;
    juce::uint64 cleanGraphCounter = 0;
    juce::uint64 cleanRouterCounter = 0;
    std::atomic<bool> pluginChanged { false };
    std::atomic<double> ignorePluginChangesUntilMs { 0.0 };
    bool lastReportedDirty = false;
    std::map<InstanceId, juce::AudioProcessor*> listenedPlugins;

    // Autosave.
    int autosaveIntervalSeconds = kDefaultAutosaveSeconds;
    double lastAutosaveMs = 0.0;
    int numAutosaves = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ProjectController)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectController)
};

} // namespace vstplayer
