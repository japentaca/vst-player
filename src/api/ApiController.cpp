#include "api/ApiController.h"

#include "api/ApiServer.h"
#include "engine/AudioEngine.h"
#include "engine/GainProcessor.h"
#include "engine/MidiRouter.h"
#include "engine/MidiSourceProcessor.h"
#include "project/Project.h"
#include "project/ProjectController.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <condition_variable>
#include <mutex>
#include <set>

namespace vstplayer
{

namespace
{
using Obj = juce::DynamicObject;

juce::String stateName (Instance::State state)
{
    switch (state)
    {
        case Instance::State::loading: return "loading";
        case Instance::State::ready:   return "ready";
        case Instance::State::failed:  return "failed";
        case Instance::State::missing: return "missing";
    }

    return "unknown";
}

bool isNumber (const juce::var& v)
{
    return v.isInt() || v.isInt64() || v.isDouble();
}

double toDb (float gain)
{
    return juce::Decibels::gainToDecibels (gain, -100.0f);
}

juce::var routeToVar (const MidiRoute& route)
{
    auto o = new Obj();
    o->setProperty ("inputDevice", route.inputDevice);

    juce::Array<juce::var> channels;
    for (int ch = 1; ch <= 16; ++ch)
        if (route.hasChannel (ch))
            channels.add (ch);

    o->setProperty ("channels", channels);
    o->setProperty ("instanceId", route.instanceId);

    if (route.isKeep())
    {
        o->setProperty ("channelMode", "keep");
    }
    else
    {
        auto remap = new Obj();
        remap->setProperty ("remapTo", route.remapChannel);
        o->setProperty ("channelMode", juce::var (remap));
    }

    return juce::var (o);
}

// Resolves a path given by a client: absolute as is, relative to `base` otherwise.
juce::File resolvePath (const juce::String& path, const juce::File& base)
{
    const auto p = path.trim().unquoted();
    return juce::File::isAbsolutePath (p) ? juce::File (p) : base.getChildFile (p);
}

const juce::StringArray& commandNames()
{
    static const juce::StringArray names { "status",      "loadProject", "listProjects", "saveProject", "listParams",
                                           "getParam",    "setParam",    "setGain",      "setMute",     "panic",
                                           "listPrograms", "setProgram", "midi",         "levels",      "audioStats" };
    return names;
}
} // namespace

//==============================================================================
ApiController::ApiController (AudioEngine& e, InstanceGraph& g, MidiRouter& r, ProjectController& p)
    : engine (e), graph (g), router (r), project (p)
{
    weakThis = this; // created here, copied by the network threads
    graph.addListener (this);
    project.addChangeListener (this);
    lastLoadSerial = project.getLoadSerial();
}

ApiController::~ApiController()
{
    stop();
    project.removeChangeListener (this);
    graph.removeListener (this);

    std::vector<InstanceId> ids;
    for (auto& [id, entry] : live)
        ids.push_back (id);

    for (auto& id : ids)
        removeLive (id);

    masterReference.clear();
}

bool ApiController::start (const Config& newConfig)
{
    JUCE_ASSERT_MESSAGE_THREAD
    stop();

    config = newConfig;
    error.clear();

    if (! config.enabled)
        return true;

    stopping = false;
    server = std::make_unique<api::ApiServer>();
    auto* s = server.get();
    server->onClientsChanged = [this, s] { numClients = s->getNumClients(); };

    std::string startError;
    const auto started = server->start (kHost, config.port, config.token.toStdString(),
                                        [this] (const std::string& body)
                                        {
                                            api::ApiServer::Response r;
                                            r.body = handleRequest (body, r.status);
                                            return r;
                                        },
                                        startError);

    if (! started)
    {
        error = juce::String::fromUTF8 (startError.c_str());
        server = nullptr;
        return false;
    }

    // Current instances: events report changes from here on.
    knownStates.clear();
    for (auto& instance : graph.getInstances())
        knownStates[instance->id] = instance->state;

    syncLive();
    lastLoadSerial = project.getLoadSerial();
    startTimerHz (kEventHz);
    return true;
}

void ApiController::stop()
{
    stopping = true;
    stopTimer();

    for (auto& waiter : loadWaiters)
        waiter (fail (503, "stopping", "El servidor de la API se está cerrando."));

    loadWaiters.clear();

    if (server != nullptr)
    {
        server->stop(); // joins the network threads (pending requests answer 503)
        server = nullptr;
    }

    numClients = 0;
}

bool ApiController::isRunning() const
{
    return server != nullptr && server->isRunning();
}

int ApiController::getNumClients() const
{
    return numClients.load();
}

juce::String ApiController::getStatusText() const
{
    if (! config.enabled)
        return "API: desactivada";

    if (error.isNotEmpty())
        return "API: " + error;

    if (! isRunning())
        return "API: detenida";

    const auto n = getNumClients();
    return "API: " + juce::String (kHost) + ":" + juce::String (config.port) + ", " + juce::String (n)
           + (n == 1 ? " cliente" : " clientes");
}

//==============================================================================
ApiController::Reply ApiController::ok (juce::DynamicObject::Ptr data)
{
    auto o = new Obj();
    o->setProperty ("ok", true);

    if (data != nullptr)
        for (auto& p : data->getProperties())
            o->setProperty (p.name, p.value);

    return { 200, juce::var (o) };
}

ApiController::Reply ApiController::fail (int status, const juce::String& code, const juce::String& message)
{
    auto o = new Obj();
    o->setProperty ("ok", false);
    o->setProperty ("code", code);
    o->setProperty ("error", message);
    return { status, juce::var (o) };
}

std::string ApiController::handleRequest (const std::string& body, int& status)
{
    juce::var request;
    Reply reply;
    juce::String cmd;

    const auto parsed = juce::JSON::parse (juce::String::fromUTF8 (body.data(), (int) body.size()), request);

    if (parsed.failed())
    {
        reply = fail (400, "badJson", juce::String::fromUTF8 ("JSON inválido: ") + parsed.getErrorMessage());
    }
    else if (request.getDynamicObject() == nullptr)
    {
        reply = fail (400, "badJson", juce::String::fromUTF8 ("El mensaje debe ser un objeto JSON con \"cmd\"."));
    }
    else
    {
        cmd = request["cmd"].toString();

        struct Entry
        {
            Command command;
            int timeoutMs;
        };

        static const std::map<juce::String, Entry> table {
            { "status", { &ApiController::cmdStatus, kDefaultTimeoutMs } },
            { "loadProject", { &ApiController::cmdLoadProject, kLoadTimeoutMs } },
            { "listProjects", { &ApiController::cmdListProjects, kDefaultTimeoutMs } },
            { "saveProject", { &ApiController::cmdSaveProject, 60000 } },
            { "listParams", { &ApiController::cmdListParams, 30000 } },
            { "getParam", { &ApiController::cmdGetParam, kDefaultTimeoutMs } },
            { "setGain", { &ApiController::cmdSetGain, kDefaultTimeoutMs } },
            { "setMute", { &ApiController::cmdSetMute, kDefaultTimeoutMs } },
            { "panic", { &ApiController::cmdPanic, kDefaultTimeoutMs } },
            { "listPrograms", { &ApiController::cmdListPrograms, kDefaultTimeoutMs } },
            { "setProgram", { &ApiController::cmdSetProgram, kDefaultTimeoutMs } },
            { "levels", { &ApiController::cmdLevels, kDefaultTimeoutMs } },
            { "audioStats", { &ApiController::cmdAudioStats, kDefaultTimeoutMs } },
        };

        if (cmd == "setParam")
            reply = setParamDirect (request);
        else if (cmd == "midi")
            reply = midiDirect (request);
        else if (const auto it = table.find (cmd); it != table.end())
            reply = runOnMessageThread (it->second.command, request, it->second.timeoutMs);
        else
            reply = fail (400, "unknownCommand",
                          (cmd.isEmpty() ? juce::String ("Falta \"cmd\".") : "Comando desconocido: \"" + cmd + "\".")
                              + " Comandos: " + commandNames().joinIntoString (", ") + ".");
    }

    if (auto* o = reply.body.getDynamicObject())
    {
        if (request.isObject() && request.hasProperty ("id"))
            o->setProperty ("id", request["id"]);

        if (cmd.isNotEmpty())
            o->setProperty ("cmd", cmd);
    }

    status = reply.status;
    return juce::JSON::toString (reply.body, true).toStdString();
}

ApiController::Reply ApiController::runOnMessageThread (Command command, const juce::var& request, int timeoutMs)
{
    struct Pending
    {
        std::mutex lock;
        std::condition_variable condition;
        bool done = false;
        Reply reply;
    };

    if (stopping.load())
        return fail (503, "stopping", "El servidor de la API se está cerrando.");

    auto pending = std::make_shared<Pending>();
    Responder respond = [pending] (Reply r)
    {
        {
            std::lock_guard<std::mutex> l (pending->lock);

            if (pending->done)
                return;

            pending->done = true;
            pending->reply = std::move (r);
        }

        pending->condition.notify_all();
    };

    // The controller lives on the message thread and is destroyed there, after stop() joined
    // every network thread; the posted call checks it is still alive anyway.
    const auto posted = juce::MessageManager::callAsync ([weak = weakThis, command, request, respond]
    {
        auto* self = weak.get();

        if (self == nullptr || self->stopping.load())
        {
            respond (fail (503, "stopping", "El servidor de la API se está cerrando."));
            return;
        }

        (self->*command) (request, respond);
    });

    if (! posted)
        return fail (503, "stopping", "La aplicación se está cerrando.");

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds (timeoutMs);
    std::unique_lock<std::mutex> l (pending->lock);

    while (! pending->done)
    {
        if (stopping.load())
            return fail (503, "stopping", "El servidor de la API se está cerrando.");

        if (std::chrono::steady_clock::now() > deadline)
            return fail (504, "timeout", "El comando no terminó en " + juce::String (timeoutMs / 1000) + " s.");

        pending->condition.wait_for (l, std::chrono::milliseconds (50));
    }

    return pending->reply;
}

//==============================================================================
// Network thread: parameters and MIDI.

std::shared_ptr<ApiController::Live> ApiController::findLiveLocked (const juce::String& idOrName) const
{
    if (const auto it = live.find (idOrName); it != live.end())
        return it->second;

    for (auto& [id, entry] : live)
        if (entry->name == idOrName)
            return entry;

    return nullptr;
}

ApiController::Reply ApiController::setParamDirect (const juce::var& request)
{
    const auto id = request["instanceId"].toString();

    if (id.isEmpty())
        return fail (400, "badRequest", "Falta \"instanceId\".");

    if (! isNumber (request["value"]))
        return fail (400, "badRequest", juce::String::fromUTF8 ("Falta \"value\" (número normalizado 0..1)."));

    const auto value = juce::jlimit (0.0f, 1.0f, (float) (double) request["value"]);

    const juce::ScopedLock sl (liveLock);
    const auto entry = findLiveLocked (id);

    if (entry == nullptr)
        return fail (404, "notFound", "No hay una instancia cargada con id o nombre \"" + id + "\".");

    int index = -1;

    if (request.hasProperty ("index"))
    {
        if (! isNumber (request["index"]))
            return fail (400, "badRequest", juce::String::fromUTF8 ("\"index\" debe ser un número."));

        index = (int) request["index"];
    }
    else if (request.hasProperty ("name"))
    {
        const auto it = entry->indexByName.find (request["name"].toString());
        index = it != entry->indexByName.end() ? it->second : -2;
    }
    else if (request.hasProperty ("paramId"))
    {
        const auto it = entry->indexById.find (request["paramId"].toString());
        index = it != entry->indexById.end() ? it->second : -2;
    }
    else
    {
        return fail (400, "badRequest", juce::String::fromUTF8 ("Falta \"index\", \"name\" o \"paramId\" del parámetro."));
    }

    const auto& params = entry->plugin->getParameters();

    if (index < 0 || index >= params.size())
        return fail (404, "paramNotFound", juce::String::fromUTF8 ("La instancia no tiene ese parámetro (")
                                               + (index == -2 ? "nombre/id desconocido" : "índice fuera de rango") + ").");

    auto* param = params[index];
    param->setValueNotifyingHost (value); // safe from any thread (see class comment)

    auto data = new Obj();
    data->setProperty ("instanceId", entry->id);
    data->setProperty ("index", index);
    data->setProperty ("name", (size_t) index < entry->paramNames.size() ? entry->paramNames[(size_t) index] : juce::String());
    data->setProperty ("value", param->getValue());
    return ok (data);
}

ApiController::Reply ApiController::midiDirect (const juce::var& request)
{
    const auto id = request["instanceId"].toString();

    if (id.isEmpty())
        return fail (400, "badRequest", "Falta \"instanceId\".");

    const auto* bytes = request["bytes"].getArray();

    if (bytes == nullptr || bytes->isEmpty() || bytes->size() > 3 * 256)
        return fail (400, "badRequest", "\"bytes\" debe ser un array de bytes MIDI, p. ej. [144, 60, 100].");

    std::vector<juce::MidiMessage> messages;

    for (int i = 0; i < bytes->size();)
    {
        const auto& b = bytes->getReference (i);

        if (! isNumber (b) || (int) b < 0x80 || (int) b > 0xEF)
            return fail (400, "badMidi", "Byte " + juce::String (i)
                                             + juce::String::fromUTF8 (": se esperaba un status de canal (128..239). Sin running status, sysex ni mensajes de sistema."));

        const auto statusByte = (int) b;
        const auto type = statusByte & 0xf0;
        const auto length = (type == 0xc0 || type == 0xd0) ? 2 : 3;

        if (i + length > bytes->size())
            return fail (400, "badMidi", juce::String::fromUTF8 ("Mensaje MIDI incompleto al final de \"bytes\"."));

        int data[2] = { 0, 0 };

        for (int k = 1; k < length; ++k)
        {
            const auto& d = bytes->getReference (i + k);

            if (! isNumber (d) || (int) d < 0 || (int) d > 127)
                return fail (400, "badMidi", "Byte " + juce::String (i + k) + juce::String::fromUTF8 (": dato fuera de 0..127."));

            data[k - 1] = (int) d;
        }

        messages.push_back (length == 2 ? juce::MidiMessage (statusByte, data[0]) : juce::MidiMessage (statusByte, data[0], data[1]));
        i += length;
    }

    const juce::ScopedLock sl (liveLock);
    const auto entry = findLiveLocked (id);

    if (entry == nullptr || entry->midi == nullptr)
        return fail (404, "notFound", "No hay una instancia cargada con id o nombre \"" + id + "\".");

    for (auto& m : messages)
        entry->midi->addMessage (m); // timestamp 0 = now: next audio block

    entry->apiMidiCount.fetch_add ((juce::int64) messages.size());

    auto data = new Obj();
    data->setProperty ("instanceId", entry->id);
    data->setProperty ("messages", (int) messages.size());
    return ok (data);
}

//==============================================================================
// Message thread commands.

Instance* ApiController::findReadyInstance (const juce::var& request, Reply& err) const
{
    const auto id = request["instanceId"].toString();

    if (id.isEmpty())
    {
        err = fail (400, "badRequest", "Falta \"instanceId\".");
        return nullptr;
    }

    auto* instance = graph.findInstance (id);

    if (instance == nullptr)
        for (auto& candidate : graph.getInstances())
            if (candidate->name == id)
            {
                instance = candidate.get();
                break;
            }

    if (instance == nullptr)
    {
        err = fail (404, "notFound", "No hay una instancia con id o nombre \"" + id + "\".");
        return nullptr;
    }

    if (! instance->isReady())
    {
        err = fail (409, "notReady", "La instancia \"" + instance->name + juce::String::fromUTF8 ("\" no está cargada (estado: ")
                                         + stateName (instance->state) + ").");
        return nullptr;
    }

    return instance;
}

juce::var ApiController::describeInstance (const Instance& instance) const
{
    auto o = new Obj();
    o->setProperty ("id", instance.id);
    o->setProperty ("name", instance.name);
    o->setProperty ("state", stateName (instance.state));

    if (instance.errorMessage.isNotEmpty())
        o->setProperty ("error", instance.errorMessage);

    const auto& d = instance.description;
    auto plugin = new Obj();
    plugin->setProperty ("name", d.name);
    plugin->setProperty ("manufacturer", d.manufacturerName);
    plugin->setProperty ("version", d.version);
    plugin->setProperty ("format", d.pluginFormatName);
    plugin->setProperty ("fileOrIdentifier", d.fileOrIdentifier);
    plugin->setProperty ("uniqueId", juce::String::toHexString (d.uniqueId).paddedLeft ('0', 8)); // as in the project file
    plugin->setProperty ("isInstrument", d.isInstrument);
    o->setProperty ("plugin", juce::var (plugin));

    o->setProperty ("gainDb", instance.gainDb);
    o->setProperty ("mute", instance.mute);
    o->setProperty ("editorOpen", instance.window.open);

    if (auto* p = instance.getPlugin(); p != nullptr && instance.isReady())
    {
        o->setProperty ("numParams", p->getParameters().size());
        o->setProperty ("numPrograms", p->getNumPrograms());

        if (p->getNumPrograms() > 0)
        {
            const auto current = p->getCurrentProgram();
            o->setProperty ("currentProgram", current);
            o->setProperty ("programName", p->getProgramName (current));
        }
    }

    juce::Array<juce::var> routes;
    for (auto& route : router.getRoutesForInstance (instance.id))
        routes.add (routeToVar (route));

    o->setProperty ("midiRoutes", routes);
    return juce::var (o);
}

juce::var ApiController::describeProject() const
{
    auto o = new Obj();
    o->setProperty ("name", project.getProjectName());
    o->setProperty ("path", project.hasFile() ? project.getProjectFile().getFullPathName() : juce::String());
    o->setProperty ("dirty", project.isDirty());
    o->setProperty ("loading", project.isLoading());
    o->setProperty ("pendingLoads", project.getNumPendingLoads());
    return juce::var (o);
}

juce::var ApiController::describeParameter (juce::AudioProcessorParameter& p, int index) const
{
    auto o = new Obj();
    o->setProperty ("index", index);
    o->setProperty ("name", p.getName (256));

    if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (&p))
        o->setProperty ("id", hosted->getParameterID());

    o->setProperty ("value", p.getValue());
    o->setProperty ("text", p.getCurrentValueAsText());
    o->setProperty ("label", p.getLabel());
    o->setProperty ("default", p.getDefaultValue());

    if (p.isDiscrete())
        o->setProperty ("steps", p.getNumSteps());

    o->setProperty ("automatable", p.isAutomatable());
    return juce::var (o);
}

void ApiController::cmdStatus (const juce::var&, const Responder& respond)
{
    auto data = new Obj();
    data->setProperty ("project", describeProject());

    auto audio = new Obj();
    audio->setProperty ("deviceType", engine.getDeviceTypeName());
    audio->setProperty ("device", engine.getDeviceName());
    audio->setProperty ("sampleRate", engine.getSampleRate());
    audio->setProperty ("bufferSize", engine.getBufferSize());
    const auto sr = engine.getSampleRate();
    audio->setProperty ("outputLatencyMs", sr > 0.0 ? 1000.0 * engine.getOutputLatencySamples() / sr : 0.0);
    audio->setProperty ("cpu", engine.getCpuUsage() * 100.0);
    data->setProperty ("audio", juce::var (audio));

    juce::Array<juce::var> instances;
    for (auto& instance : graph.getInstances())
        instances.add (describeInstance (*instance));

    data->setProperty ("instances", instances);

    juce::Array<juce::var> routes;
    for (auto& route : router.getRoutes())
        routes.add (routeToVar (route));

    data->setProperty ("midiRoutes", routes);

    auto apiInfo = new Obj();
    apiInfo->setProperty ("host", kHost);
    apiInfo->setProperty ("port", config.port);
    apiInfo->setProperty ("clients", getNumClients());
    apiInfo->setProperty ("version", 1);
    apiInfo->setProperty ("app", juce::JUCEApplication::getInstance() != nullptr ? juce::JUCEApplication::getInstance()->getApplicationVersion()
                                                                                : juce::String());
    data->setProperty ("api", juce::var (apiInfo));

    respond (ok (data));
}

void ApiController::cmdLoadProject (const juce::var& request, const Responder& respond)
{
    const auto path = request["path"].toString();

    if (path.isEmpty())
        return respond (fail (400, "badRequest", "Falta \"path\" (ruta del .vstproj.json)."));

    const auto base = getDefaultProjectFolder ? getDefaultProjectFolder() : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    const auto file = resolvePath (path, base);

    if (! file.existsAsFile())
        return respond (fail (404, "notFound", "No existe el archivo " + file.getFullPathName()));

    if (project.isDirty() && ! (bool) request["discardChanges"])
        return respond (fail (409, "unsavedChanges",
                              "El proyecto actual \"" + project.getProjectName()
                                  + juce::String::fromUTF8 ("\" tiene cambios sin guardar. Guardarlo antes (saveProject) o mandar \"discardChanges\": true.")));

    const auto result = project.open (file);

    if (result.failed())
    {
        if (onStatusMessage)
            onStatusMessage ("API: no se pudo abrir " + file.getFullPathName() + ": " + result.getErrorMessage(), true);

        return respond (fail (400, "failed", result.getErrorMessage()));
    }

    if (onStatusMessage)
        onStatusMessage ("API: abierto " + file.getFullPathName(), false);

    if (project.isLoading())
        loadWaiters.push_back (respond); // answered by changeListenerCallback once loaded
    else
        respond (makeLoadReply());
}

ApiController::Reply ApiController::makeLoadReply() const
{
    auto data = new Obj();
    data->setProperty ("project", describeProject());
    data->setProperty ("warnings", project.getLastWarnings());

    juce::Array<juce::var> instances;
    for (auto& instance : graph.getInstances())
        instances.add (describeInstance (*instance));

    data->setProperty ("instances", instances);

    return ok (data);
}

void ApiController::resolveLoadWaiters()
{
    if (loadWaiters.empty())
        return;

    const auto reply = makeLoadReply();
    auto waiters = std::move (loadWaiters);
    loadWaiters.clear();

    for (auto& waiter : waiters)
        waiter (reply);
}

void ApiController::cmdListProjects (const juce::var& request, const Responder& respond)
{
    const auto base = getDefaultProjectFolder ? getDefaultProjectFolder() : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    const auto dirText = request["dir"].toString();
    const auto dir = dirText.isNotEmpty() ? resolvePath (dirText, base) : base;

    if (! dir.isDirectory())
        return respond (fail (404, "notFound", "No existe la carpeta " + dir.getFullPathName()));

    auto files = dir.findChildFiles (juce::File::findFiles, (bool) request["recursive"], juce::String ("*") + project::kFileSuffix);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFullPathName().compareNatural (b.getFullPathName()) < 0; });

    constexpr int kMaxFiles = 500;
    juce::Array<juce::var> projects;

    for (auto& f : files)
    {
        if (projects.size() >= kMaxFiles)
            break;

        auto o = new Obj();
        o->setProperty ("name", project::nameFromFile (f));
        o->setProperty ("path", f.getFullPathName());
        o->setProperty ("size", (juce::int64) f.getSize());
        o->setProperty ("modified", f.getLastModificationTime().toISO8601 (true));
        projects.add (juce::var (o));
    }

    auto data = new Obj();
    data->setProperty ("dir", dir.getFullPathName());
    data->setProperty ("projects", projects);
    data->setProperty ("truncated", files.size() > kMaxFiles);

    juce::Array<juce::var> recent;
    for (auto& p : project.getRecentProjects().getPaths())
        recent.add (p);

    data->setProperty ("recent", recent);
    data->setProperty ("current", project.hasFile() ? project.getProjectFile().getFullPathName() : juce::String());
    respond (ok (data));
}

void ApiController::cmdSaveProject (const juce::var& request, const Responder& respond)
{
    juce::Result result = juce::Result::ok();
    const auto path = request["path"].toString();

    if (path.isNotEmpty())
    {
        const auto base = getDefaultProjectFolder ? getDefaultProjectFolder() : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        result = project.saveAs (resolvePath (path, base));
    }
    else if (! project.hasFile())
    {
        return respond (fail (409, "noFile", juce::String::fromUTF8 ("El proyecto no tiene archivo todavía: mandar \"path\".")));
    }
    else
    {
        result = project.save();
    }

    if (result.failed())
    {
        if (onStatusMessage)
            onStatusMessage ("API: no se pudo guardar: " + result.getErrorMessage(), true);

        return respond (fail (500, "failed", result.getErrorMessage()));
    }

    if (onStatusMessage)
        onStatusMessage ("API: guardado " + project.getProjectFile().getFullPathName(), false);

    auto data = new Obj();
    data->setProperty ("path", project.getProjectFile().getFullPathName());
    data->setProperty ("name", project.getProjectName());
    respond (ok (data));
}

void ApiController::refreshParameterCache (juce::AudioPluginInstance& plugin)
{
    juce::MemoryBlock state;
    plugin.getStateInformation (state);

    if (state.getSize() > 0)
        plugin.setStateInformation (state.getData(), (int) state.getSize());
}

void ApiController::refreshThen (const InstanceId& id, std::function<void()> then)
{
    // Wait until the pending parameter / program changes reached the plugin (they travel to its
    // processor with the next audio blocks): restoring a state taken before that would undo them.
    const auto sampleRate = engine.getSampleRate();
    const auto blockMs = sampleRate > 0.0 ? 1000.0 * engine.getBufferSize() / sampleRate : 10.0;
    const auto delayMs = juce::jmax (100, juce::roundToInt (4.0 * blockMs));

    juce::Timer::callAfterDelay (delayMs, [weak = weakThis, id, then = std::move (then)]
    {
        auto* self = weak.get();

        if (self == nullptr)
            return; // shutting down: the waiting request answers 503

        if (auto* instance = self->graph.findInstance (id); instance != nullptr && instance->isReady())
            refreshParameterCache (*instance->getPlugin());

        then();
    });
}

void ApiController::cmdListParams (const juce::var& request, const Responder& respond)
{
    Reply err;
    auto* instance = findReadyInstance (request, err);

    if (instance == nullptr)
        return respond (err);

    auto* plugin = instance->getPlugin();

    // Optional: re-read every value from the plugin. The host keeps a cache of parameter values
    // that goes stale when the plugin changes them without telling the host (Dexed changing
    // program, sforzando loading an instrument). Re-applying the plugin's own state refreshes it,
    // but it is a full state restore (a sampler reloads its instrument).
    if ((bool) request["refresh"])
    {
        auto copy = request.clone();
        copy.getDynamicObject()->removeProperty ("refresh");
        return refreshThen (instance->id, [this, copy, respond] { cmdListParams (copy, respond); });
    }

    const auto filter = request["filter"].toString();
    const auto offset = juce::jmax (0, (int) request["offset"]);
    const auto limit = request.hasProperty ("limit") ? juce::jmax (0, (int) request["limit"]) : std::numeric_limits<int>::max();

    juce::Array<juce::var> params;
    int matched = 0;
    const auto& all = plugin->getParameters();

    for (int i = 0; i < all.size(); ++i)
    {
        auto* p = all[i];

        if (filter.isNotEmpty() && ! p->getName (256).containsIgnoreCase (filter))
            continue;

        if (matched++ < offset || params.size() >= limit)
            continue;

        params.add (describeParameter (*p, i));
    }

    auto data = new Obj();
    data->setProperty ("instanceId", instance->id);
    data->setProperty ("total", all.size());
    data->setProperty ("matched", matched);
    data->setProperty ("params", params);
    respond (ok (data));
}

void ApiController::cmdGetParam (const juce::var& request, const Responder& respond)
{
    Reply err;
    auto* instance = findReadyInstance (request, err);

    if (instance == nullptr)
        return respond (err);

    const auto& all = instance->getPlugin()->getParameters();
    int index = -1;

    if (request.hasProperty ("index"))
        index = (int) request["index"];
    else
        for (int i = 0; i < all.size() && index < 0; ++i)
            if ((request.hasProperty ("name") && all[i]->getName (1024) == request["name"].toString())
                || (request.hasProperty ("paramId") && dynamic_cast<juce::HostedAudioProcessorParameter*> (all[i]) != nullptr
                    && static_cast<juce::HostedAudioProcessorParameter*> (all[i])->getParameterID() == request["paramId"].toString()))
                index = i;

    if (index < 0 || index >= all.size())
        return respond (fail (404, "paramNotFound", juce::String::fromUTF8 ("La instancia no tiene ese parámetro.")));

    auto data = new Obj();
    data->setProperty ("instanceId", instance->id);
    data->setProperty ("param", describeParameter (*all[index], index));
    respond (ok (data));
}

void ApiController::cmdSetGain (const juce::var& request, const Responder& respond)
{
    Reply err;
    auto* instance = findReadyInstance (request, err);

    if (instance == nullptr)
        return respond (err);

    const auto& value = request.hasProperty ("gainDb") ? request["gainDb"] : request["value"];

    if (! isNumber (value))
        return respond (fail (400, "badRequest", juce::String::fromUTF8 ("Falta \"gainDb\" (número, dB).")));

    graph.setGainDb (instance->id, (float) (double) value);

    if (onInstanceValuesChanged)
        onInstanceValuesChanged();

    auto data = new Obj();
    data->setProperty ("instanceId", instance->id);
    data->setProperty ("gainDb", instance->gainDb);
    respond (ok (data));
}

void ApiController::cmdSetMute (const juce::var& request, const Responder& respond)
{
    Reply err;
    auto* instance = findReadyInstance (request, err);

    if (instance == nullptr)
        return respond (err);

    const auto& value = request.hasProperty ("mute") ? request["mute"] : request["value"];

    if (! value.isBool() && ! isNumber (value))
        return respond (fail (400, "badRequest", "Falta \"mute\" (true / false)."));

    graph.setMute (instance->id, (bool) value);

    if (onInstanceValuesChanged)
        onInstanceValuesChanged();

    auto data = new Obj();
    data->setProperty ("instanceId", instance->id);
    data->setProperty ("mute", instance->mute);
    respond (ok (data));
}

void ApiController::cmdPanic (const juce::var&, const Responder& respond)
{
    if (panicAction)
        panicAction();
    else
        router.panic();

    if (onStatusMessage)
        onStatusMessage (juce::String::fromUTF8 ("API: pánico (all notes off + all sound off)."), false);

    respond (ok());
}

void ApiController::cmdListPrograms (const juce::var& request, const Responder& respond)
{
    Reply err;
    auto* instance = findReadyInstance (request, err);

    if (instance == nullptr)
        return respond (err);

    auto* plugin = instance->getPlugin();
    juce::Array<juce::var> programs;

    for (int i = 0; i < plugin->getNumPrograms(); ++i)
    {
        auto o = new Obj();
        o->setProperty ("index", i);
        o->setProperty ("name", plugin->getProgramName (i));
        programs.add (juce::var (o));
    }

    auto data = new Obj();
    data->setProperty ("instanceId", instance->id);
    data->setProperty ("current", plugin->getNumPrograms() > 0 ? plugin->getCurrentProgram() : -1);
    data->setProperty ("programs", programs);
    respond (ok (data));
}

void ApiController::cmdSetProgram (const juce::var& request, const Responder& respond)
{
    Reply err;
    auto* instance = findReadyInstance (request, err);

    if (instance == nullptr)
        return respond (err);

    auto* plugin = instance->getPlugin();

    if (! isNumber (request["index"]))
        return respond (fail (400, "badRequest", juce::String::fromUTF8 ("Falta \"index\" (número de programa).")));

    const auto index = (int) request["index"];

    if (index < 0 || index >= plugin->getNumPrograms())
        return respond (fail (404, "programNotFound", "Programa fuera de rango (0.." + juce::String (plugin->getNumPrograms() - 1) + ")."));

    plugin->setCurrentProgram (index);

    const auto reply = [this, id = instance->id, respond]
    {
        auto* inst = graph.findInstance (id);

        if (inst == nullptr || ! inst->isReady())
            return respond (fail (409, "notReady", juce::String::fromUTF8 ("La instancia ya no está cargada.")));

        auto* p = inst->getPlugin();
        auto data = new Obj();
        data->setProperty ("instanceId", id);
        data->setProperty ("current", p->getCurrentProgram());
        data->setProperty ("name", p->getProgramName (p->getCurrentProgram()));
        respond (ok (data));
    };

    reply();
}

void ApiController::cmdLevels (const juce::var&, const Responder& respond)
{
    juce::Array<juce::var> instances;

    for (auto& instance : graph.getInstances())
    {
        if (auto* gain = instance->getGainProcessor())
        {
            auto o = new Obj();
            o->setProperty ("instanceId", instance->id);
            o->setProperty ("name", instance->name);
            o->setProperty ("peakDb", toDb (gain->getAndResetPeak()));
            o->setProperty ("blocks", gain->getBlocksProcessed());
            instances.add (juce::var (o));
        }
    }

    auto data = new Obj();
    data->setProperty ("instances", instances);

    if (auto* mixer = engine.getMixer())
        data->setProperty ("masterPeakDb", toDb (mixer->getAndResetPeak()));

    respond (ok (data));
}

void ApiController::cmdAudioStats (const juce::var&, const Responder& respond)
{
    const auto stats = engine.getAndResetCallbackStats();

    auto data = new Obj();
    data->setProperty ("sampleRate", engine.getSampleRate());
    data->setProperty ("bufferSize", engine.getBufferSize());
    data->setProperty ("cpu", engine.getCpuUsage() * 100.0);
    data->setProperty ("xruns", engine.getXRunCount());
    data->setProperty ("callbacks", stats.callbacks);
    data->setProperty ("totalCallbacks", stats.totalCallbacks);
    data->setProperty ("callbackAvgMs", stats.averageMs);
    data->setProperty ("callbackMaxMs", stats.maxMs);
    data->setProperty ("blockMs", stats.budgetMs);
    data->setProperty ("overBudget", stats.overBudget);

    if (auto* mixer = engine.getMixer())
        data->setProperty ("mixerBlocks", mixer->getBlocksProcessed());

    respond (ok (data));
}

//==============================================================================
// Live table and events.

void ApiController::syncLive (const InstanceId& excluded)
{
    JUCE_ASSERT_MESSAGE_THREAD

    std::set<InstanceId> wanted;

    for (auto& instance : graph.getInstances())
    {
        if (! instance->isReady() || instance->id == excluded)
            continue;

        wanted.insert (instance->id);
        auto* plugin = instance->getPlugin();

        if (const auto it = live.find (instance->id); it != live.end())
        {
            if (it->second->plugin == plugin)
            {
                if (it->second->name != instance->name)
                {
                    const juce::ScopedLock sl (liveLock);
                    it->second->name = instance->name;
                }

                continue;
            }

            removeLive (instance->id); // plugin replaced
        }

        auto entry = std::make_shared<Live>();
        entry->id = instance->id;
        entry->name = instance->name;
        entry->plugin = plugin;
        entry->midi = instance->getMidiSource();

        const auto& params = plugin->getParameters();
        entry->numParams = params.size();
        entry->dirty = std::make_unique<std::atomic<bool>[]> ((size_t) juce::jmax (1, entry->numParams));

        for (int i = 0; i < params.size(); ++i)
        {
            entry->dirty[(size_t) i] = false;
            const auto name = params[i]->getName (1024);
            entry->paramNames.push_back (name);
            entry->indexByName.emplace (name, i); // duplicated names: the first one wins

            if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (params[i]))
                entry->indexById.emplace (hosted->getParameterID(), i);
        }

        {
            const juce::ScopedLock sl (liveLock);
            live[entry->id] = entry;
        }

        {
            const juce::SpinLock::ScopedLockType sl (processorLock);
            liveByProcessor[plugin] = entry;
        }

        plugin->addListener (this);
    }

    std::vector<InstanceId> stale;
    for (auto& [id, entry] : live)
        if (wanted.count (id) == 0)
            stale.push_back (id);

    for (auto& id : stale)
        removeLive (id);
}

void ApiController::removeLive (const InstanceId& id)
{
    std::shared_ptr<Live> entry;

    {
        const juce::ScopedLock sl (liveLock);
        const auto it = live.find (id);

        if (it == live.end())
            return;

        entry = it->second;
        live.erase (it);
    }

    {
        const juce::SpinLock::ScopedLockType sl (processorLock);
        liveByProcessor.erase (entry->plugin);
    }

    entry->plugin->removeListener (this);
}

void ApiController::instanceListChanged()
{
    std::map<InstanceId, Instance::State> current;

    for (auto& instance : graph.getInstances())
    {
        current[instance->id] = instance->state;

        if (server != nullptr && knownStates.count (instance->id) == 0)
        {
            auto data = new Obj();
            data->setProperty ("instance", describeInstance (*instance));
            sendEvent ("instanceAdded", data);
        }
    }

    for (auto& [id, state] : knownStates)
    {
        if (server != nullptr && current.count (id) == 0)
        {
            auto data = new Obj();
            data->setProperty ("instanceId", id);
            sendEvent ("instanceRemoved", data);
        }
    }

    knownStates = std::move (current);
    syncLive();
}

void ApiController::instanceAboutToBeRemoved (const Instance& instance)
{
    // Before the nodes go away: the network threads must not reach this plugin any more.
    removeLive (instance.id);
}

void ApiController::instanceLoadFinished (const Instance& instance)
{
    syncLive();

    if (server != nullptr)
    {
        auto data = new Obj();
        data->setProperty ("instance", describeInstance (instance));
        sendEvent ("instanceLoaded", data);
    }
}

void ApiController::audioProcessorParameterChanged (juce::AudioProcessor* processor, int parameterIndex, float)
{
    // Any thread (audio thread included): only flags the parameter.
    if (numClients.load() == 0)
        return;

    const juce::SpinLock::ScopedLockType sl (processorLock);

    if (const auto it = liveByProcessor.find (processor); it != liveByProcessor.end())
    {
        auto& entry = *it->second;

        if (parameterIndex >= 0 && parameterIndex < entry.numParams)
        {
            entry.dirty[(size_t) parameterIndex] = true;
            entry.anyDirty = true;
        }
    }
}

void ApiController::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (project.isLoading() || project.getLoadSerial() == lastLoadSerial)
        return;

    lastLoadSerial = project.getLoadSerial();

    if (server != nullptr)
    {
        auto data = new Obj();
        data->setProperty ("project", describeProject());
        data->setProperty ("warnings", project.getLastWarnings());
        data->setProperty ("numInstances", (int) graph.getInstances().size());
        sendEvent ("projectLoaded", data);
    }

    resolveLoadWaiters();
}

void ApiController::timerCallback()
{
    const auto hasClients = numClients.load() > 0;

    for (auto& [id, entry] : live)
    {
        // MIDI activity: messages routed from the ports + sent through the API.
        const auto activity = router.getActivityCount (id) + entry->apiMidiCount.load();

        if (hasClients && entry->lastActivity >= 0 && activity != entry->lastActivity)
        {
            auto data = new Obj();
            data->setProperty ("instanceId", id);
            data->setProperty ("count", activity);
            sendEvent ("midiActivity", data);
        }

        entry->lastActivity = activity;

        if (! entry->anyDirty.exchange (false))
            continue;

        juce::Array<juce::var> changes;
        bool truncated = false;
        const auto& params = entry->plugin->getParameters();

        for (int i = 0; i < entry->numParams && i < params.size(); ++i)
        {
            if (! entry->dirty[(size_t) i].exchange (false))
                continue;

            if (changes.size() >= kMaxParamsPerEvent)
            {
                truncated = true;
                continue;
            }

            auto o = new Obj();
            o->setProperty ("index", i);
            o->setProperty ("name", entry->paramNames[(size_t) i]);
            o->setProperty ("value", params[i]->getValue());
            o->setProperty ("text", params[i]->getCurrentValueAsText());
            changes.add (juce::var (o));
        }

        if (hasClients && ! changes.isEmpty())
        {
            auto data = new Obj();
            data->setProperty ("instanceId", id);
            data->setProperty ("params", changes);

            if (truncated)
                data->setProperty ("truncated", true);

            sendEvent ("paramChanged", data);
        }
    }
}

void ApiController::sendEvent (const juce::String& name, juce::DynamicObject::Ptr data)
{
    if (server == nullptr || numClients.load() == 0)
        return;

    auto o = new Obj();
    o->setProperty ("event", name);

    if (data != nullptr)
        for (auto& p : data->getProperties())
            o->setProperty (p.name, p.value);

    server->broadcast (juce::JSON::toString (juce::var (o), true).toStdString());
}

} // namespace vstplayer
