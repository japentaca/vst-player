#include "project/ProjectController.h"

#include "engine/AudioEngine.h"
#include "engine/MidiRouter.h"
#include "engine/PluginManager.h"
#include "project/ProjectSerializer.h"

namespace vstplayer
{

namespace
{
constexpr int kTimerMs = 250;
// Plugins often report parameter / program changes on their own right after being created or
// after their state was restored (asynchronous restartComponent, parameter dispatchers...).
// Those are not user edits: ignore plugin notifications for this long after a load.
constexpr double kPluginSettleMs = 2500.0;

const juce::String untitledName()
{
    return juce::String::fromUTF8 ("Sin título");
}

juce::PluginDescription descriptionFromRef (const project::PluginRef& ref)
{
    juce::PluginDescription d;
    d.name = ref.name.isNotEmpty() ? ref.name : juce::String ("(sin nombre)");
    d.descriptiveName = d.name;
    d.manufacturerName = ref.manufacturer;
    d.fileOrIdentifier = ref.fileOrIdentifier;
    d.uniqueId = ref.uniqueId.getHexValue32();
    d.version = ref.version;
    d.pluginFormatName = ref.format;
    d.isInstrument = true;
    return d;
}

juce::AudioProcessorParameter* findParameterByName (juce::AudioProcessor& processor, const juce::String& name)
{
    for (auto* p : processor.getParameters())
        if (p->getName (1024) == name)
            return p; // duplicated names: the first one wins (documented)

    return nullptr;
}
} // namespace

//==============================================================================
ProjectController::ProjectController (AudioEngine& e, PluginManager& p, InstanceGraph& g, MidiRouter& r,
                                      const juce::File& userDataDirectory)
    : engine (e),
      plugins (p),
      graph (g),
      router (r),
      dataDirectory (userDataDirectory),
      recent (userDataDirectory != juce::File() ? userDataDirectory.getChildFile ("recent.json") : juce::File()),
      projectName (untitledName())
{
    graph.addListener (this);
    markClean();
    lastAutosaveMs = juce::Time::getMillisecondCounterHiRes();
    startTimer (kTimerMs);
}

ProjectController::~ProjectController()
{
    stopTimer();
    detachAllPlugins();
    graph.removeListener (this);
}

void ProjectController::detachAllPlugins()
{
    for (auto& [id, processor] : listenedPlugins)
        if (auto* instance = graph.findInstance (id); instance != nullptr && instance->getPlugin() == processor)
            processor->removeListener (this);

    listenedPlugins.clear();
}

//==============================================================================
bool ProjectController::isDirty() const
{
    return explicitDirty || pluginChanged.load() || graph.getEditCounter() != cleanGraphCounter
           || router.getEditCounter() != cleanRouterCounter;
}

void ProjectController::markDirty()
{
    explicitDirty = true;
    lastReportedDirty = true;
    sendChangeMessage();
}

void ProjectController::markClean()
{
    explicitDirty = false;
    pluginChanged = false;
    cleanGraphCounter = graph.getEditCounter();
    cleanRouterCounter = router.getEditCounter();
    lastReportedDirty = false;
}

juce::String ProjectController::getDisplayName() const
{
    return projectName + (isDirty() ? " *" : "");
}

juce::String ProjectController::describeProgress() const
{
    if (! isLoading())
        return {};

    return "Cargando proyecto: " + juce::String (loadsRequested - getNumPendingLoads()) + " de " + juce::String (loadsRequested)
           + juce::String::fromUTF8 (" instancias…");
}

//==============================================================================
project::PluginRef ProjectController::makePluginRef (const juce::PluginDescription& d)
{
    project::PluginRef ref;
    ref.name = d.name;
    ref.manufacturer = d.manufacturerName;
    ref.fileOrIdentifier = d.fileOrIdentifier;
    ref.uniqueId = juce::String::toHexString (d.uniqueId).paddedLeft ('0', 8);
    ref.version = d.version;
    ref.format = d.pluginFormatName.isNotEmpty() ? d.pluginFormatName : juce::String ("VST3");
    return ref;
}

std::optional<juce::PluginDescription> ProjectController::resolvePlugin (const project::PluginRef& ref) const
{
    if (ref.format.isNotEmpty() && ! ref.format.equalsIgnoreCase ("VST3"))
        return std::nullopt; // only VST3 is hosted

    const auto types = plugins.getKnownPluginList().getTypes();

    // Several matches (e.g. a .vst3 with more than one plugin inside): prefer the same name.
    const auto pick = [&ref] (const juce::Array<juce::PluginDescription>& matches) -> std::optional<juce::PluginDescription>
    {
        if (matches.isEmpty())
            return std::nullopt;

        for (auto& d : matches)
            if (d.name == ref.name)
                return d;

        return matches.getFirst();
    };

    // 1. uniqueId
    if (ref.uniqueId.trim().isNotEmpty())
    {
        const auto uid = ref.uniqueId.trim().getHexValue32();
        juce::Array<juce::PluginDescription> matches;

        for (auto& d : types)
            if (d.uniqueId == uid)
                matches.add (d);

        if (auto found = pick (matches))
            return found;
    }

    // 2. file path
    if (ref.fileOrIdentifier.isNotEmpty())
    {
        juce::Array<juce::PluginDescription> matches;

        for (auto& d : types)
            if (d.fileOrIdentifier.equalsIgnoreCase (ref.fileOrIdentifier))
                matches.add (d);

        if (auto found = pick (matches))
            return found;
    }

    // 3. name (same manufacturer first)
    if (ref.name.isNotEmpty())
    {
        juce::Array<juce::PluginDescription> matches;

        for (auto& d : types)
            if (d.name.equalsIgnoreCase (ref.name))
                matches.add (d);

        for (auto& d : matches)
            if (d.manufacturerName.equalsIgnoreCase (ref.manufacturer))
                return d;

        if (! matches.isEmpty())
            return matches.getFirst();
    }

    return std::nullopt;
}

//==============================================================================
project::Project ProjectController::capture()
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (syncEditorWindows)
        syncEditorWindows();

    project::Project p;
    p.version = juce::jmax (project::kCurrentVersion, loadedVersion);
    p.name = projectName;
    p.extra = projectExtra;

    p.audio = loadedAudio;

    if (engine.getDeviceManager().getCurrentAudioDevice() != nullptr)
    {
        p.audio.present = true;
        p.audio.deviceType = engine.getDeviceTypeName();
        p.audio.outputDevice = engine.getDeviceName();
        p.audio.sampleRate = engine.getSampleRate();
        p.audio.bufferSize = engine.getBufferSize();
    }

    for (auto& instance : graph.getInstances())
    {
        project::InstanceData data;
        const auto carriedIt = carried.find (instance->id);
        const auto hasCarried = carriedIt != carried.end();

        if (hasCarried)
            data = carriedIt->second;

        data.id = instance->id;
        data.name = instance->name;
        data.gainDb = instance->gainDb;
        data.mute = instance->mute;

        if (auto* plugin = instance->isReady() ? instance->getPlugin() : nullptr)
        {
            auto ref = makePluginRef (instance->description);
            ref.extra = data.plugin.extra;
            data.plugin = ref;

            juce::MemoryBlock state;
            plugin->getStateInformation (state);
            data.state = project::serializer::encodeState (state);

            // `params` overrides stay in the file but follow the current values, so that the
            // next load does not undo what the user changed in the editor.
            for (auto& param : data.params)
                if (auto* p2 = findParameterByName (*plugin, param.name))
                    param.value = p2->getValue();

            data.window.open = instance->window.open;
            data.window.hasPosition = instance->window.hasPosition;
            data.window.x = instance->window.position.x;
            data.window.y = instance->window.position.y;
        }
        else if (hasCarried)
        {
            // Missing, failed or still loading: keep what the file had (plugin, state, window).
            if (instance->window.hasPosition)
            {
                data.window.hasPosition = true;
                data.window.x = instance->window.position.x;
                data.window.y = instance->window.position.y;
            }
        }
        else
        {
            data.plugin = makePluginRef (instance->description);
            data.window.open = false;
            data.window.hasPosition = instance->window.hasPosition;
            data.window.x = instance->window.position.x;
            data.window.y = instance->window.position.y;
        }

        p.instances.push_back (std::move (data));
    }

    for (auto& route : router.getRoutes())
    {
        project::MidiRouteData data;
        data.inputDevice = route.inputDevice;
        data.channels = project::maskToChannels (route.channelMask);
        data.instanceId = route.instanceId;
        data.remapTo = route.isKeep() ? 0 : route.remapChannel;

        for (auto& loaded : loadedRoutes)
            if (loaded.inputDevice == data.inputDevice && loaded.instanceId == data.instanceId)
                data.extra = loaded.extra;

        p.midiRoutes.push_back (std::move (data));
    }

    return p;
}

juce::Result ProjectController::save()
{
    if (! hasFile())
        return juce::Result::fail ("El proyecto no tiene archivo: usar \"Guardar como...\".");

    const auto result = project::serializer::saveFile (capture(), projectFile);

    if (result.wasOk())
    {
        markClean();
        recent.add (projectFile);

        if (auto backup = getAutosaveFile(); backup.existsAsFile())
            backup.deleteFile(); // the real file is newer than the backup now
    }

    sendChangeMessage();
    return result;
}

juce::Result ProjectController::saveAs (const juce::File& file)
{
    const auto target = project::withProjectSuffix (file);
    const auto previousFile = projectFile;
    const auto previousName = projectName;

    projectFile = target;
    projectName = project::nameFromFile (target);

    const auto result = save();

    if (result.failed())
    {
        projectFile = previousFile;
        projectName = previousName;
        sendChangeMessage();
    }

    return result;
}

juce::Result ProjectController::open (const juce::File& file)
{
    project::Project p;
    juce::StringArray warnings;
    const auto result = project::serializer::loadFile (file, p, &warnings);

    if (result.failed())
    {
        if (! file.existsAsFile())
            recent.remove (file);

        return result;
    }

    apply (p, file, warnings);
    return juce::Result::ok();
}

void ProjectController::newProject()
{
    apply (project::Project(), juce::File());
}

void ProjectController::apply (const project::Project& p, const juce::File& file, juce::StringArray warnings)
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Tear down the current setup (editors close through InstancePanel's listener; pending
    // asynchronous loads are dropped when they arrive).
    pendingLoads.clear();
    openEditorAfterLoad.clear();
    graph.removeAllInstances();
    router.setRoutes ({});
    detachAllPlugins();
    carried.clear();

    projectFile = file;
    projectName = p.name.trim().isNotEmpty() ? p.name.trim() : file != juce::File() ? project::nameFromFile (file) : untitledName();
    loadedVersion = p.version;
    projectExtra = p.extra;
    loadedAudio = p.audio;
    loadedRoutes = p.midiRoutes;

    // Audio first, so the plugins are created at the project's sample rate.
    if (p.audio.present)
        warnings.addArray (engine.applyDeviceSetup (p.audio.deviceType, p.audio.outputDevice, p.audio.sampleRate, p.audio.bufferSize));

    std::map<juce::String, InstanceId> idMap; // file id -> id in the graph (differs only for duplicates)

    for (auto data : p.instances)
    {
        InstanceGraph::AddOptions options;
        options.id = data.id;
        options.name = data.name.isNotEmpty() ? data.name : data.plugin.name;
        options.gainDb = (float) data.gainDb;
        options.mute = data.mute;
        options.window.hasPosition = data.window.hasPosition;
        options.window.position = { data.window.x, data.window.y };

        InstanceId id;

        if (const auto description = resolvePlugin (data.plugin))
        {
            juce::WeakReference<ProjectController> weakThis (this);
            options.prepare = [weakThis, data] (juce::AudioPluginInstance& plugin)
            {
                if (auto* self = weakThis.get())
                {
                    juce::StringArray problems;
                    self->restoreState (plugin, data, problems);
                    self->lastWarnings.addArray (problems);
                }
            };

            id = graph.addInstance (*description, std::move (options));
            pendingLoads.insert (id);

            if (data.window.open)
                openEditorAfterLoad.insert (id);
        }
        else
        {
            const auto shownName = data.plugin.name.isNotEmpty() ? data.plugin.name : juce::String ("(sin nombre)");
            const auto message = juce::String::fromUTF8 ("el plugin no está instalado o no está en la lista de plugins")
                                 + (data.plugin.fileOrIdentifier.isNotEmpty() ? " (" + data.plugin.fileOrIdentifier + ")" : juce::String());
            id = graph.addMissingInstance (descriptionFromRef (data.plugin), options, message);
            warnings.add ("Falta el plugin \"" + shownName + "\" de la instancia \"" + options.name + "\": " + message
                          + juce::String::fromUTF8 (". Se conserva en el proyecto (con su estado) al volver a guardar."));
        }

        if (! idMap.count (data.id))
            idMap[data.id] = id;

        data.id = id;
        carried[id] = std::move (data);
    }

    std::vector<MidiRoute> routes;

    for (auto& data : p.midiRoutes)
    {
        MidiRoute route;
        route.inputDevice = data.inputDevice;
        route.channelMask = project::channelsToMask (data.channels);
        route.instanceId = idMap.count (data.instanceId) ? idMap[data.instanceId] : data.instanceId;
        route.remapChannel = data.remapTo;
        routes.push_back (route);

        if (graph.findInstance (route.instanceId) == nullptr)
            warnings.add ("La ruta MIDI de \"" + data.inputDevice + "\" apunta a una instancia que no existe (" + data.instanceId + ").");
        else if (data.inputDevice.isNotEmpty() && ! router.getAvailableInputNames().contains (data.inputDevice))
            warnings.add ("El puerto MIDI \"" + data.inputDevice
                          + juce::String::fromUTF8 ("\" no existe ahora; la ruta queda inactiva hasta que aparezca."));
    }

    router.setRoutes (std::move (routes));

    loadsRequested = (int) pendingLoads.size();
    lastWarnings = warnings;
    markClean();

    if (file.existsAsFile())
        recent.add (file);

    sendChangeMessage();

    if (pendingLoads.empty())
        finishLoading();
}

void ProjectController::restoreState (juce::AudioPluginInstance& plugin, const project::InstanceData& data,
                                      juce::StringArray& problems) const
{
    if (data.state.isNotEmpty())
    {
        juce::MemoryBlock state;

        if (project::serializer::decodeState (data.state, state) && state.getSize() > 0)
            plugin.setStateInformation (state.getData(), (int) state.getSize());
        else
            problems.add ("\"" + data.name + juce::String::fromUTF8 ("\": el estado guardado no es base64 válido; el plugin queda con su estado inicial."));
    }

    for (auto& param : data.params)
    {
        if (auto* p = findParameterByName (plugin, param.name))
            p->setValueNotifyingHost ((float) param.value);
        else
            problems.add ("\"" + data.name + juce::String::fromUTF8 ("\": el plugin no tiene un parámetro llamado \"") + param.name + "\".");
    }
}

void ProjectController::finishLoading()
{
    ++loadSerial;
    openEditorAfterLoad.clear();
    sendChangeMessage();

    if (! lastWarnings.isEmpty() && onLoadWarnings)
        onLoadWarnings (lastWarnings);
}

//==============================================================================
void ProjectController::instanceListChanged() {}

void ProjectController::instanceAboutToBeRemoved (const Instance& instance)
{
    if (const auto it = listenedPlugins.find (instance.id); it != listenedPlugins.end())
    {
        if (instance.getPlugin() == it->second)
            it->second->removeListener (this);

        listenedPlugins.erase (it);
    }

    carried.erase (instance.id);

    if (pendingLoads.erase (instance.id) > 0 && pendingLoads.empty())
        juce::MessageManager::callAsync ([weakThis = juce::WeakReference<ProjectController> (this)]
                                         {
                                             if (auto* self = weakThis.get(); self != nullptr && ! self->isLoading())
                                                 self->finishLoading();
                                         });
}

void ProjectController::instanceLoadFinished (const Instance& instance)
{
    if (auto* plugin = instance.isReady() ? instance.getPlugin() : nullptr)
    {
        ignorePluginChangesUntilMs = juce::Time::getMillisecondCounterHiRes() + kPluginSettleMs;

        if (listenedPlugins.count (instance.id) == 0)
        {
            plugin->addListener (this);
            listenedPlugins[instance.id] = plugin;
        }
    }

    if (pendingLoads.erase (instance.id) == 0)
        return; // added from the UI, not by a project

    if (instance.state == Instance::State::failed)
        lastWarnings.add (instance.errorMessage);

    if (instance.isReady() && openEditorAfterLoad.count (instance.id) > 0)
    {
        const auto id = instance.id;
        juce::MessageManager::callAsync ([weakThis = juce::WeakReference<ProjectController> (this), id]
                                         {
                                             if (auto* self = weakThis.get(); self != nullptr && self->openEditor)
                                                 self->openEditor (id);
                                         });
    }

    if (pendingLoads.empty())
        finishLoading();
    else
        sendChangeMessage();
}

//==============================================================================
void ProjectController::notePluginChange()
{
    if (juce::Time::getMillisecondCounterHiRes() < ignorePluginChangesUntilMs.load())
        return;

    pluginChanged = true;
}

void ProjectController::audioProcessorParameterChanged (juce::AudioProcessor*, int, float)
{
    notePluginChange();
}

void ProjectController::audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details)
{
    if (details.programChanged || details.nonParameterStateChanged)
        notePluginChange();
}

//==============================================================================
void ProjectController::timerCallback()
{
    const auto dirty = isDirty();

    if (dirty != lastReportedDirty)
    {
        lastReportedDirty = dirty;
        sendChangeMessage();
    }

    const auto now = juce::Time::getMillisecondCounterHiRes();

    if (dirty && ! isLoading() && now - lastAutosaveMs >= autosaveIntervalSeconds * 1000.0)
    {
        lastAutosaveMs = now;

        if (getAutosaveFile() != juce::File())
            writeAutosave();
    }
    else if (! dirty)
    {
        lastAutosaveMs = now; // the interval counts from the first unsaved change
    }
}

juce::File ProjectController::getAutosaveFile() const
{
    return dataDirectory != juce::File() ? dataDirectory.getChildFile (kAutosaveFileName) : juce::File();
}

void ProjectController::setAutosaveIntervalSeconds (int seconds)
{
    autosaveIntervalSeconds = juce::jmax (1, seconds);
}

juce::Result ProjectController::writeAutosave()
{
    const auto file = getAutosaveFile();

    if (file == juce::File())
        return juce::Result::fail ("Autoguardado desactivado (sin carpeta de datos).");

    const auto result = project::serializer::saveFile (capture(), file);

    if (result.wasOk())
        ++numAutosaves;
    else
        DBG ("Autosave failed: " << result.getErrorMessage());

    return result;
}

} // namespace vstplayer
