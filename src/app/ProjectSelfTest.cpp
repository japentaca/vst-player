// "--project-selftest <phase>" (stage 5): save a project, quit, open it again in a new process
// (command line, then "Recientes") and compare everything with what was saved. Driven by
// tools/run-project-test.ps1, which runs the phases in order with the same --project-test-dir
// and --project-data-dir (a temporary folder: the user's recent.json / autosave are not touched).
//
//   save           loads Dexed, Surge XT and sforzando, changes their state (Dexed program +
//                  parameters, parameters in the others), gains, mute, a name, MIDI routes (keep,
//                  remapTo, a port that does not exist, two routes for one instance), editor
//                  windows (one open, one closed after moving it); saves <dir>\prueba.vstproj.json
//                  with "Guardar como" and writes the expected fingerprint (every parameter value,
//                  program, MD5 of getStateInformation, windows, routes, audio).
//   verify-cli     started with "<dir>\prueba.vstproj.json" as argument: waits for the load and
//                  compares the live setup with the fingerprint.
//   verify-recent  started without a project: opens Recientes[0] (the menu path) and compares.
//   open           VstPlayer.exe <project> --project-selftest open --project-test-dir <any>: waits
//                  for the load and prints every instance and warning (checks example projects).
//   extras         a project with a plugin that does not exist + unknown JSON fields + a `params`
//                  override: opens with a warning, missing instance in red, override applied;
//                  unsaved-changes flag, title "*", autosave, the "cambios sin guardar" question
//                  (cancel / save), re-save keeps the missing instance and every unknown field;
//                  an invalid file does not replace the open project.

#include "app/CommandLine.h"
#include "app/InstanceSelfTest.h"
#include "app/MainWindow.h"
#include "engine/AudioEngine.h"
#include "engine/GainProcessor.h"
#include "engine/InstanceGraph.h"
#include "engine/MidiRouter.h"
#include "engine/PluginManager.h"
#include "project/ProjectController.h"
#include "project/ProjectSerializer.h"
#include "ui/InstancePanel.h"
#include "ui/PluginEditorWindow.h"

#include <map>

namespace vstplayer
{

namespace
{
constexpr double kLoadTimeoutSeconds = 120.0;
constexpr const char* kProjectFileName = "prueba.vstproj.json";
constexpr const char* kFingerprintFileName = "fingerprint.expected";
constexpr const char* kExtrasFileName = "extras.vstproj.json";
constexpr const char* kResavedFileName = "extras-resaved.vstproj.json";
constexpr const char* kMissingPluginName = "PluginQueNoExiste";
constexpr const char* kGhostPort = "Puerto MIDI Fantasma";
constexpr const char* kLoopMidiPort = "loopMIDI Port";
constexpr float kParamTolerance = 1.0e-4f;

const juce::Point<int> kDexedEditorPos { 140, 160 };
const juce::Point<int> kSurgeEditorPos { 260, 210 };

struct ProjectTestState
{
    juce::PluginDescription dexed, surge, sforzando;
    InstanceId dexedId, surgeId, sforzandoId, missingId;
    juce::var expected; // fingerprint read from disk
    juce::String dexedOverrideParam;
    int messagesBefore = 0;
    juce::StringArray messages;
    int promptsBefore = 0;
    std::map<juce::String, float> measuredPeaks; // instance id -> peak of the test note
};

ProjectTestState& pstate()
{
    static ProjectTestState s;
    return s;
}

bool isUsableParameter (juce::AudioProcessorParameter& p)
{
    const auto name = p.getName (256);
    return p.isAutomatable() && ! p.isMetaParameter() && name.isNotEmpty() && ! name.containsIgnoreCase ("bypass")
           && ! name.containsIgnoreCase ("program") && ! name.startsWithIgnoreCase ("MIDI CC");
}

// 64-bit FNV-1a (juce_cryptography is not linked): enough to tell two states apart.
juce::String hashOf (const juce::MemoryBlock& block)
{
    juce::uint64 hash = 14695981039346656037ull;

    for (size_t i = 0; i < block.getSize(); ++i)
    {
        hash ^= static_cast<const juce::uint8*> (block.getData())[i];
        hash *= 1099511628211ull;
    }

    return juce::String::toHexString ((juce::int64) hash);
}

// Snapshot of everything a project should restore, as JSON.
juce::var makeFingerprint (InstanceGraph& graph, MidiRouter& router, AudioEngine& engine, InstancePanel& panel)
{
    auto root = juce::DynamicObject::Ptr (new juce::DynamicObject());

    auto audio = juce::DynamicObject::Ptr (new juce::DynamicObject());
    audio->setProperty ("type", engine.getDeviceTypeName());
    audio->setProperty ("device", engine.getDeviceName());
    audio->setProperty ("sampleRate", engine.getSampleRate());
    audio->setProperty ("bufferSize", engine.getBufferSize());
    root->setProperty ("audio", juce::var (audio.get()));

    juce::Array<juce::var> instances;

    for (auto& instance : graph.getInstances())
    {
        auto o = juce::DynamicObject::Ptr (new juce::DynamicObject());
        o->setProperty ("id", instance->id);
        o->setProperty ("name", instance->name);
        o->setProperty ("plugin", instance->description.name);
        o->setProperty ("gainDb", instance->gainDb);
        o->setProperty ("mute", instance->mute);
        o->setProperty ("ready", instance->isReady());

        auto* window = panel.getEditorWindow (instance->id);
        o->setProperty ("windowOpen", window != nullptr);
        const auto pos = window != nullptr ? window->getPosition() : instance->window.position;
        o->setProperty ("hasPosition", window != nullptr || instance->window.hasPosition);
        o->setProperty ("x", pos.x);
        o->setProperty ("y", pos.y);

        if (auto* plugin = instance->getPlugin())
        {
            juce::MemoryBlock state;
            plugin->getStateInformation (state);
            o->setProperty ("stateHash", hashOf (state));
            o->setProperty ("stateSize", (int) state.getSize());
            o->setProperty ("program", plugin->getCurrentProgram());
            o->setProperty ("programName", plugin->getNumPrograms() > 0 ? plugin->getProgramName (plugin->getCurrentProgram()) : juce::String());

            juce::Array<juce::var> values, names;

            for (auto* p : plugin->getParameters())
            {
                values.add (p->getValue());
                names.add (p->getName (256));
            }

            o->setProperty ("params", values);
            o->setProperty ("paramNames", names);
        }

        instances.add (juce::var (o.get()));
    }

    root->setProperty ("instances", instances);

    juce::StringArray routes;
    for (auto& r : router.getRoutes())
        routes.add (r.inputDevice + " | canales " + MidiRoute::describeChannels (r.channelMask) + " | " + r.instanceId + " | "
                    + (r.isKeep() ? juce::String ("keep") : "remapTo " + juce::String (r.remapChannel)));

    routes.sort (false);
    juce::Array<juce::var> routeArray;
    for (auto& r : routes)
        routeArray.add (r);

    root->setProperty ("routes", routeArray);
    return juce::var (root.get());
}

// sforzando (ARIA engine) keeps its whole state in the VST3 component chunk:
//   u32 0, u32 n, "CEGP", u32 xmlLength, zlib(<AriaSave> XML)        (n = 8 + zlib size)
// wrapped by JUCE's host in <VST3PluginState><IComponent>base64</IComponent>. Its parameters are
// 512 macros that are not saved without an instrument, so the test changes the state itself:
// reads the XML, lets `modify` edit it and builds a new state for setStateInformation.
juce::String readAriaXml (const juce::MemoryBlock& vst3State, juce::MemoryBlock* chunkOut = nullptr)
{
    auto xml = juce::AudioProcessor::getXmlFromBinary (vst3State.getData(), (int) vst3State.getSize());
    auto* component = xml != nullptr ? xml->getChildByName ("IComponent") : nullptr;

    if (component == nullptr)
        return {};

    juce::MemoryBlock chunk;

    if (! chunk.fromBase64Encoding (component->getAllSubText().trim()) || chunk.getSize() < 16
        || juce::String::createStringFromData (static_cast<const char*> (chunk.getData()) + 8, 4) != "CEGP")
        return {};

    if (chunkOut != nullptr)
        *chunkOut = chunk;

    auto* source = new juce::MemoryInputStream (static_cast<const char*> (chunk.getData()) + 16, chunk.getSize() - 16, false);
    juce::GZIPDecompressorInputStream unzip (source, true, juce::GZIPDecompressorInputStream::zlibFormat);
    return unzip.readEntireStreamAsString();
}

juce::MemoryBlock writeAriaXml (const juce::MemoryBlock& vst3State, const juce::String& ariaXml)
{
    juce::MemoryBlock compressed;

    {
        juce::MemoryOutputStream out (compressed, false);
        juce::GZIPCompressorOutputStream zip (out, 9);
        zip.write (ariaXml.toRawUTF8(), ariaXml.getNumBytesAsUTF8());
    }

    juce::MemoryOutputStream chunk;
    chunk.writeInt (0);
    chunk.writeInt ((int) compressed.getSize() + 8);
    chunk.write ("CEGP", 4);
    chunk.writeInt ((int) ariaXml.getNumBytesAsUTF8());
    chunk.write (compressed.getData(), compressed.getSize());

    auto xml = juce::AudioProcessor::getXmlFromBinary (vst3State.getData(), (int) vst3State.getSize());
    auto* component = xml->getChildByName ("IComponent");
    component->deleteAllTextElements();
    component->addTextElement (chunk.getMemoryBlock().toBase64Encoding());

    juce::MemoryBlock result;
    juce::AudioProcessor::copyXmlToBinary (*xml, result);
    return result;
}

const juce::var* findById (const juce::var& instances, const juce::String& id)
{
    if (auto* array = instances.getArray())
        for (auto& v : *array)
            if (v["id"].toString() == id)
                return &v;

    return nullptr;
}
} // namespace

//==============================================================================
void InstanceSelfTest::setProjectTest (ProjectController& controller, const juce::String& phase, const juce::File& testDir,
                                       const juce::File& commandLineProject)
{
    project = &controller;
    projectPhase = phase;
    projectTestDir = testDir;
    projectFromCommandLine = commandLineProject;
}

void InstanceSelfTest::clearProjectTestState()
{
    pstate() = ProjectTestState();
}

void InstanceSelfTest::buildProjectSteps()
{
    auto& s = pstate();
    s = ProjectTestState();

    if (project == nullptr || projectTestDir == juce::File())
    {
        action ("Argumentos", [this] { return check (false, "faltan --project-selftest <fase> y --project-test-dir <carpeta>"); });
        return;
    }

    auto& pc = *project;
    auto& panel = window.getInstancePanel();
    const auto dir = projectTestDir;
    const auto projectFile = dir.getChildFile (kProjectFileName);
    const auto fingerprintFile = dir.getChildFile (kFingerprintFileName);

    window.onMessage = [&s] (const juce::String& text)
    {
        s.messages.add (text);
        cli::print ("  [mensaje] " + text.replace ("\n", " / "));
    };

    const auto isLoaded = [this] (const InstanceId& id)
    {
        auto* instance = graph.findInstance (id);
        return instance != nullptr && instance->state != Instance::State::loading;
    };

    const auto findPlugins = [this, &s]
    {
        auto ok = true;

        for (auto [name, target] : { std::pair { "Dexed", &s.dexed }, std::pair { "Surge XT", &s.surge }, std::pair { "sforzando", &s.sforzando } })
        {
            auto found = false;

            for (auto& d : plugins.getKnownPluginList().getTypes())
                if (d.name == name && d.isInstrument && ! found)
                {
                    *target = d;
                    found = true;
                }

            ok = check (found, juce::String (name) + (found ? " en plugins.xml" : " NO esta en plugins.xml")) && ok;
        }

        return ok;
    };

    const auto idByPlugin = [this] (const juce::String& pluginName) -> InstanceId
    {
        for (auto& instance : graph.getInstances())
            if (instance->description.name == pluginName)
                return instance->id;

        return {};
    };

    // Compares the live setup with the saved fingerprint.
    const auto compareWithExpected = [this, &s, &panel] (const juce::String& label)
    {
        const auto live = makeFingerprint (graph, router, engine, panel);
        const auto& expected = s.expected;
        auto ok = true;

        ok = check (live["audio"]["type"] == expected["audio"]["type"] && live["audio"]["device"] == expected["audio"]["device"]
                        && (double) live["audio"]["sampleRate"] == (double) expected["audio"]["sampleRate"]
                        && (int) live["audio"]["bufferSize"] == (int) expected["audio"]["bufferSize"],
                    label + ": audio " + live["audio"]["type"].toString() + " / " + live["audio"]["device"].toString() + " "
                        + live["audio"]["sampleRate"].toString() + " Hz, buffer " + live["audio"]["bufferSize"].toString())
             && ok;

        ok = check (live["instances"].size() == expected["instances"].size(),
                    label + ": " + juce::String (live["instances"].size()) + " instancias (esperadas "
                        + juce::String (expected["instances"].size()) + ")")
             && ok;

        for (auto& e : *expected["instances"].getArray())
        {
            const auto id = e["id"].toString();
            const auto* l = findById (live["instances"], id);
            const auto who = label + ": " + e["plugin"].toString() + " \"" + e["name"].toString() + "\"";

            if (! check (l != nullptr, who + " existe con el mismo id " + id))
            {
                ok = false;
                continue;
            }

            const auto& v = *l;
            ok = check (v["ready"] == juce::var (true), who + " cargado") && ok;
            ok = check (v["name"] == e["name"] && v["plugin"] == e["plugin"], who + " nombre y plugin") && ok;
            ok = check (std::abs ((double) v["gainDb"] - (double) e["gainDb"]) < 1.0e-6 && v["mute"] == e["mute"],
                        who + " ganancia " + v["gainDb"].toString() + " dB, mute " + v["mute"].toString())
                 && ok;
            ok = check (v["windowOpen"] == e["windowOpen"] && v["hasPosition"] == e["hasPosition"] && (int) v["x"] == (int) e["x"]
                            && (int) v["y"] == (int) e["y"],
                        who + " ventana del editor " + (v["windowOpen"] ? juce::String ("abierta") : juce::String ("cerrada"))
                            + (v["hasPosition"] ? " en (" + v["x"].toString() + ", " + v["y"].toString() + ")" : juce::String (" sin posicion"))
                            + " (esperada " + (e["windowOpen"] ? juce::String ("abierta") : juce::String ("cerrada")) + " en ("
                            + e["x"].toString() + ", " + e["y"].toString() + "))")
                 && ok;
            ok = check ((int) v["program"] == (int) e["program"] && v["programName"] == e["programName"],
                        who + " programa " + v["program"].toString() + " \"" + v["programName"].toString() + "\"")
                 && ok;

            auto* lp = v["params"].getArray();
            auto* ep = e["params"].getArray();
            auto mismatches = 0;

            if (lp != nullptr && ep != nullptr && lp->size() == ep->size())
            {
                for (int i = 0; i < ep->size(); ++i)
                {
                    const auto a = (float) (*lp)[i];
                    const auto b = (float) (*ep)[i];

                    // A normalised value outside 0..1 is not a real parameter value (sforzando
                    // reports e.g. 120 for some macros right after loading an instrument).
                    if (a < 0.0f || a > 1.0f || b < 0.0f || b > 1.0f)
                    {
                        if (std::abs (a - b) > kParamTolerance)
                            cli::print ("  INFO  parametro " + juce::String (i) + " \"" + e["paramNames"][i].toString() + "\" con valor fuera de 0..1: "
                                        + juce::String (a, 3) + " (al guardar " + juce::String (b, 3) + "); no se compara");
                        continue;
                    }

                    if (std::abs (a - b) > kParamTolerance)
                    {
                        if (++mismatches <= 8)
                            cli::print ("        parametro " + juce::String (i) + " \"" + e["paramNames"][i].toString() + "\": " + juce::String (a, 5)
                                        + " (esperado " + juce::String (b, 5) + ")");
                    }
                }
            }
            else
            {
                mismatches = -1;
            }

            ok = check (mismatches == 0, who + ": " + juce::String (ep != nullptr ? ep->size() : 0) + " parametros, "
                                             + (mismatches < 0 ? juce::String ("cantidad distinta") : juce::String (mismatches) + " distintos"))
                 && ok;

            // Changed parameters (the ones the save phase moved away from their defaults).
            if (auto* changed = e["changed"].getArray())
                for (auto& c : *changed)
                {
                    const auto index = (int) c["index"];
                    const auto now = lp != nullptr && index < lp->size() ? (float) (*lp)[index] : -1.0f;
                    ok = check (std::abs (now - (float) c["after"]) <= kParamTolerance,
                                who + " parametro cambiado \"" + c["name"].toString() + "\" = " + juce::String (now, 4) + " (inicial "
                                    + juce::String ((float) c["before"], 4) + ", guardado " + juce::String ((float) c["after"], 4) + ")")
                         && ok;
                }

            const auto sameState = v["stateHash"] == e["stateHash"];
            cli::print (juce::String (sameState ? "  OK    " : "  INFO  ") + who + " getStateInformation "
                        + (sameState ? "identico byte a byte (" + v["stateSize"].toString() + " bytes, hash " + v["stateHash"].toString() + ")"
                                     : "distinto en bytes: " + v["stateSize"].toString() + " vs " + e["stateSize"].toString()
                                           + " bytes (los parametros y el programa se comparan arriba)"));
        }

        const auto liveRoutes = juce::JSON::toString (live["routes"], true);
        const auto expectedRoutes = juce::JSON::toString (expected["routes"], true);
        ok = check (liveRoutes == expectedRoutes, label + ": rutas MIDI " + liveRoutes) && ok;

        if (liveRoutes != expectedRoutes)
            cli::print ("        esperadas: " + expectedRoutes);

        return ok;
    };

    const auto loadExpected = [&s, fingerprintFile, this]
    {
        s.expected = juce::JSON::parse (fingerprintFile.loadFileAsString());
        return check (s.expected["instances"].size() == 3, "huella esperada leida de " + fingerprintFile.getFullPathName());
    };

    const auto waitProjectLoaded = [this, &pc] (const juce::String& name, int expectedInstances)
    {
        waitFor (name, [this, &pc, expectedInstances]
                 { return ! pc.isLoading() && (int) graph.getInstances().size() == expectedInstances && graph.getNumLoading() == 0; },
                 kLoadTimeoutSeconds);
    };

    // Note A4 (velocity 0.8) on every instance at once; peaks after 700 ms at their gain node.
    const auto addPlayAndMeasure = [this, &s] (const juce::String& label)
    {
        action ("Nota A4 en cada instancia (" + label + ")", [this]
        {
            for (auto& instance : graph.getInstances())
            {
                if (auto* gain = instance->getGainProcessor())
                    gain->getAndResetPeak();

                graph.sendMidi (instance->id, juce::MidiMessage::noteOn (1, 69, 0.8f));
            }

            return true;
        });

        waitMs (700);

        action ("Niveles (" + label + ")", [this, &s]
        {
            auto ok = true;

            for (auto& instance : graph.getInstances())
            {
                const auto peak = instance->getGainProcessor() != nullptr ? instance->getGainProcessor()->getAndResetPeak() : 0.0f;
                s.measuredPeaks[instance->id] = peak;
                graph.sendMidi (instance->id, juce::MidiMessage::noteOff (1, 69));
                ok = check (peak > 0.0f, instance->name + ": " + (peak > 0.0f ? juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS" : juce::String ("silencio"))) && ok;
            }

            return ok;
        });

        waitMs (1500);
    };

    //==========================================================================
    if (projectPhase == "save")
    {
        action ("Preparar carpeta de prueba y buscar plugins", [this, dir, findPlugins, &pc]
        {
            auto ok = check (dir.isDirectory() || dir.createDirectory().wasOk(), "carpeta " + dir.getFullPathName());
            ok = findPlugins() && ok;
            ok = check (pc.getProjectName() == juce::String::fromUTF8 ("Sin título") && ! pc.isDirty() && ! pc.hasFile(),
                        juce::String::fromUTF8 ("proyecto inicial \"Sin título\" sin cambios"))
                 && ok;
            return ok;
        });

        action ("Agregar Dexed, Surge XT y sforzando (rutas de la UI)", [&panel, &s]
        {
            s.dexedId = panel.addInstance (s.dexed);
            s.surgeId = panel.addInstance (s.surge);
            s.sforzandoId = panel.addInstance (s.sforzando);
            return true;
        });

        waitFor ("Esperar a que carguen", [isLoaded, &s] { return isLoaded (s.dexedId) && isLoaded (s.surgeId) && isLoaded (s.sforzandoId); },
                 kLoadTimeoutSeconds);

        action ("Estado: cargados y proyecto modificado", [this, &s, &pc]
        {
            auto ok = true;

            for (auto& id : { s.dexedId, s.surgeId, s.sforzandoId })
            {
                auto* instance = graph.findInstance (id);
                ok = check (instance != nullptr && instance->isReady(), (instance != nullptr ? instance->name : id) + " listo") && ok;
            }

            ok = check (pc.isDirty(), "proyecto modificado (isDirty)") && ok;
            return ok;
        });

        waitMs (400);

        action ("Titulo con *", [this]
        {
            return check (window.getName().endsWith ("*") && window.getProjectInfoText().contains ("*"), "\"" + window.getName() + "\"");
        });

        action ("Dexed: elegir otro programa (preset)", [this, &s]
        {
            auto* plugin = graph.findInstance (s.dexedId)->getPlugin();
            const auto n = plugin->getNumPrograms();
            const auto before = plugin->getCurrentProgram();
            const auto target = n > 1 ? (before == 5 % n ? 7 % n : 5 % n) : 0;
            plugin->setCurrentProgram (target);
            return check (n > 1, "Dexed tiene " + juce::String (n) + " programas; " + juce::String (before) + " -> " + juce::String (target));
        });

        waitMs (800);

        // sforzando is not in this list: its only parameters (macros, MIDI CC) are not saved by
        // sforzando without an instrument; its state is changed directly below.
        action ("Cambiar parametros de Dexed y Surge XT (3 cada uno) a valores distintos de los iniciales", [this, &s]
        {
            auto ok = true;
            auto changedRoot = juce::DynamicObject::Ptr (new juce::DynamicObject());

            for (auto& id : { s.dexedId, s.surgeId })
            {
                auto* instance = graph.findInstance (id);
                auto* plugin = instance->getPlugin();
                juce::Array<juce::var> changed;
                auto usable = 0;

                for (auto* p : plugin->getParameters())
                {
                    if (! isUsableParameter (*p))
                        continue;

                    ++usable;

                    if (changed.size() < 3 && p->getParameterIndex() >= 0)
                    {
                        auto c = juce::DynamicObject::Ptr (new juce::DynamicObject());
                        const auto before = p->getValue();
                        const auto target = before < 0.5f ? 0.83f : 0.17f;
                        p->beginChangeGesture();
                        p->setValueNotifyingHost (target);
                        p->endChangeGesture();
                        c->setProperty ("index", p->getParameterIndex());
                        c->setProperty ("name", p->getName (256));
                        c->setProperty ("before", before);
                        changed.add (juce::var (c.get()));
                    }
                }

                changedRoot->setProperty (id, changed);

                cli::print ("  " + instance->name + ": " + juce::String (plugin->getParameters().size()) + " parametros ("
                            + juce::String (usable) + " automatizables), cambiados " + juce::String (changed.size()));
                ok = check (! changed.isEmpty(), instance->name + " tiene parametros para cambiar") && ok;
            }

            s.expected = juce::var (changedRoot.get()); // temporary: changed parameters per instance
            return ok;
        });

        waitMs (1000);

        action ("sforzando: estado nuevo (globalTuning + instrumento SFZ de prueba)", [this, &s, dir]
        {
            auto* plugin = graph.findInstance (s.sforzandoId)->getPlugin();
            juce::MemoryBlock before;
            plugin->getStateInformation (before);
            const auto aria = readAriaXml (before);
            auto ok = check (aria.contains ("<AriaSave"), "estado de sforzando legible (" + juce::String (aria.length()) + " caracteres de XML)");

            const auto sfz = dir.getChildFile ("seno-de-prueba.sfz");
            sfz.replaceWithText ("// SFZ de prueba de VstPlayer: un seno generado por sforzando\n<region> sample=*sine lokey=0 hikey=127 pitch_keycenter=69\n");

            auto modified = aria.replace ("globalTuning=\"0\"", "globalTuning=\"7\"");
            modified = modified.replace ("<GUI />", "<Slot id=\"0\" name=\"" + sfz.getFullPathName() + "\" />\r\n    <GUI />");
            cli::print ("  XML nuevo: " + modified.replace ("\r\n", " ").replace ("    ", " "));
            plugin->setStateInformation (writeAriaXml (before, modified).getData(), (int) writeAriaXml (before, modified).getSize());
            return ok;
        });

        waitMs (1500);

        action ("sforzando: el estado cambio y el SFZ suena", [this, &s]
        {
            auto* instance = graph.findInstance (s.sforzandoId);
            juce::MemoryBlock after;
            instance->getPlugin()->getStateInformation (after);
            const auto aria = readAriaXml (after);
            cli::print ("  XML de sforzando: " + aria.replace ("\r\n", " ").replace ("    ", " "));
            auto ok = check (aria.contains ("globalTuning=\"7\""), "globalTuning=7 en el estado");
            ok = check (aria.contains ("seno-de-prueba.sfz"), "el estado referencia seno-de-prueba.sfz") && ok;
            graph.setGainDb (s.sforzandoId, -30.0f);
            graph.sendMidi (s.sforzandoId, juce::MidiMessage::noteOn (1, 69, 0.8f));
            return ok;
        });

        waitMs (600);

        action ("sforzando: senal con el SFZ cargado", [this, &s]
        {
            auto* instance = graph.findInstance (s.sforzandoId);
            const auto peak = instance->getGainProcessor()->getAndResetPeak();
            graph.sendMidi (s.sforzandoId, juce::MidiMessage::noteOff (1, 69));
            return check (peak > 0.0f, "pico de sforzando " + juce::String (peak > 0.0f ? juce::Decibels::gainToDecibels (peak) : -999.0f, 1) + " dBFS");
        });

        waitMs (300);

        action ("Ganancias, mute, nombre, rutas MIDI", [this, &s, &panel]
        {
            graph.setGainDb (s.dexedId, -30.0f);
            graph.setGainDb (s.surgeId, -31.5f);
            graph.setGainDb (s.sforzandoId, -33.0f);
            graph.setName (s.surgeId, "Pads Surge");

            // Same paths as the row controls.
            panel.setInstanceMidiPort (s.dexedId, kLoopMidiPort);
            panel.setInstanceMidiChannels (s.dexedId, 0x0001);
            panel.setInstanceMidiPort (s.surgeId, kLoopMidiPort);
            panel.setInstanceMidiChannels (s.surgeId, 0x0006);
            panel.setInstanceMidiRemap (s.surgeId, 1);

            // Two routes for Dexed (second one from the API), and a port that does not exist.
            auto dexedRoutes = router.getRoutesForInstance (s.dexedId);
            MidiRoute extra;
            extra.inputDevice = kLoopMidiPort;
            extra.channelMask = 1u << 9;
            extra.instanceId = s.dexedId;
            dexedRoutes.push_back (extra);
            router.setRoutesForInstance (s.dexedId, dexedRoutes);

            MidiRoute ghost;
            ghost.inputDevice = kGhostPort;
            ghost.channelMask = 1u << 3;
            ghost.instanceId = s.sforzandoId;
            router.setRoutesForInstance (s.sforzandoId, { ghost });

            return check (router.getRoutes().size() == 4, juce::String ((int) router.getRoutes().size()) + " rutas");
        });

        addPlayAndMeasure ("con el estado cambiado");

        action ("Mute de sforzando", [this, &s]
        {
            graph.setMute (s.sforzandoId, true);
            return true;
        });

        action ("Editores: Dexed abierto en (140,160); Surge XT movido a (260,210) y cerrado", [this, &s, &panel]
        {
            auto ok = panel.openEditor (s.dexedId);
            ok = panel.openEditor (s.surgeId) && ok;

            if (auto* w = panel.getEditorWindow (s.dexedId))
                w->setTopLeftPosition (kDexedEditorPos);

            if (auto* w = panel.getEditorWindow (s.surgeId))
                w->setTopLeftPosition (kSurgeEditorPos);

            return check (ok, "editores abiertos");
        });

        waitMs (1500);

        action ("Cerrar el editor de Surge XT (boton de cierre)", [this, &s, &panel]
        {
            if (auto* w = panel.getEditorWindow (s.surgeId))
                w->closeButtonPressed();

            auto* surge = graph.findInstance (s.surgeId);
            return check (panel.getEditorWindow (s.surgeId) == nullptr && surge->window.hasPosition
                              && surge->window.position == kSurgeEditorPos,
                          "Surge XT cerrado, posicion guardada (" + surge->window.position.toString() + ")");
        });

        waitMs (500);

        // JUCE's VST3 host keeps a cache of every parameter value. When a plugin changes its own
        // parameters without telling the host (Dexed after a program change, sforzando after an
        // instrument load) the cache lags behind the plugin until the next setStateInformation.
        // The state that gets saved is unaffected, but the fingerprint compares parameter values,
        // so bring the cache up to date first: give every plugin its own state back.
        action ("Sincronizar la cache de parametros del host (setStateInformation con su propio estado)", [this]
        {
            for (auto& instance : graph.getInstances())
            {
                juce::MemoryBlock state;
                instance->getPlugin()->getStateInformation (state);
                instance->getPlugin()->setStateInformation (state.getData(), (int) state.getSize());
            }

            return true;
        });

        waitMs (2000);

        action ("Guardar como " + projectFile.getFullPathName(), [this, &pc, projectFile, fingerprintFile, &s, &panel]
        {
            const auto changed = s.expected; // per-instance changed parameters
            projectFile.deleteFile();

            // Same call as the "Guardar como…" dialog callback.
            const auto result = pc.saveAs (projectFile.getSiblingFile ("prueba")); // suffix added automatically
            auto ok = check (result.wasOk(), "guardado: " + (result.wasOk() ? projectFile.getFileName() : result.getErrorMessage()));
            ok = check (pc.getProjectFile() == projectFile && projectFile.existsAsFile(), "archivo " + pc.getProjectFile().getFullPathName()) && ok;
            ok = check (! pc.isDirty(), "sin cambios sin guardar") && ok;
            ok = check (pc.getRecentProjects().getFile (0) == projectFile, "Recientes[0] = " + pc.getRecentProjects().getFile (0).getFileName()) && ok;
            ok = check (pc.getRecentProjects().getStorageFile().existsAsFile(), "recent.json en " + pc.getRecentProjects().getStorageFile().getFullPathName()) && ok;

            // Fingerprint of the live setup, plus the parameters that were changed.
            auto fingerprint = makeFingerprint (graph, router, engine, panel);

            for (auto& v : *fingerprint["instances"].getArray())
            {
                auto changedForInstance = changed[juce::Identifier (v["id"].toString())];

                if (auto* array = changedForInstance.getArray())
                    for (auto& c : *array)
                    {
                        const auto after = (float) v["params"][(int) c["index"]];
                        c.getDynamicObject()->setProperty ("after", after);
                        ok = check (std::abs (after - (float) c["before"]) > 0.02f,
                                    v["name"].toString() + ": \"" + c["name"].toString() + "\" " + juce::String ((float) c["before"], 3) + " -> "
                                        + juce::String (after, 3))
                             && ok;
                    }

                v.getDynamicObject()->setProperty ("changed", changedForInstance);
                v.getDynamicObject()->setProperty ("peak", s.measuredPeaks[v["id"].toString()]);
            }

            ok = check (fingerprintFile.replaceWithText (juce::JSON::toString (fingerprint)), "huella escrita en " + fingerprintFile.getFileName())
                 && ok;
            return ok;
        });

        waitMs (400); // the title follows the controller's (asynchronous) change messages

        action ("Titulo tras guardar", [this]
        {
            return check (! window.getName().endsWith ("*") && window.getName().contains ("prueba"), "titulo sin * : \"" + window.getName() + "\"");
        });

        action ("Contenido del archivo guardado", [this, projectFile]
        {
            project::Project p;
            juce::StringArray warnings;
            auto ok = check (project::serializer::loadFile (projectFile, p, &warnings).wasOk() && warnings.isEmpty(), "se lee sin advertencias");
            ok = check (p.version == 1 && p.name == "prueba" && p.audio.present && p.audio.deviceType.isNotEmpty(),
                        "version 1, nombre \"prueba\", audio " + p.audio.deviceType + " / " + p.audio.outputDevice + " "
                            + juce::String (p.audio.sampleRate, 0) + " Hz / " + juce::String (p.audio.bufferSize))
                 && ok;
            ok = check (p.instances.size() == 3, juce::String ((int) p.instances.size()) + " instancias") && ok;

            for (auto& i : p.instances)
                ok = check (i.state.length() > 100 && i.plugin.uniqueId.length() == 8 && i.plugin.fileOrIdentifier.endsWithIgnoreCase (".vst3"),
                            "  " + i.name + ": plugin " + i.plugin.name + " uid " + i.plugin.uniqueId + ", estado base64 "
                                + juce::String (i.state.length()) + " caracteres, ventana " + (i.window.open ? "abierta" : "cerrada")
                                + (i.window.hasPosition ? " (" + juce::String (i.window.x) + "," + juce::String (i.window.y) + ")" : juce::String()))
                     && ok;

            ok = check (p.midiRoutes.size() == 4, juce::String ((int) p.midiRoutes.size()) + " rutas MIDI") && ok;
            cli::print ("  tamano del archivo: " + juce::String (projectFile.getSize() / 1024) + " KB");
            return ok;
        });

        return;
    }

    //==========================================================================
    if (projectPhase == "verify-cli" || projectPhase == "verify-recent")
    {
        const auto fromRecent = projectPhase == "verify-recent";

        action (fromRecent ? "Abrir Recientes[0] (ruta del menu)" : "Proyecto pasado por linea de comandos", [this, &pc, fromRecent, projectFile, loadExpected]
        {
            auto ok = loadExpected();

            if (fromRecent)
            {
                ok = check (projectFromCommandLine == juce::File(), "sin proyecto en la linea de comandos") && ok;
                ok = check (graph.getInstances().empty() && pc.getProjectName() == juce::String::fromUTF8 ("Sin título"),
                            juce::String::fromUTF8 ("arranca vacío (\"Sin título\")"))
                     && ok;
                ok = check (pc.getRecentProjects().getFile (0) == projectFile,
                            "Recientes[0] = " + pc.getRecentProjects().getFile (0).getFullPathName() + " (de recent.json)")
                     && ok;
                window.openRecent (0);
            }
            else
            {
                ok = check (projectFromCommandLine == projectFile, "argumento: " + projectFromCommandLine.getFullPathName()) && ok;
            }

            ok = check (pc.getProjectFile() == projectFile, "proyecto actual: " + pc.getProjectFile().getFullPathName()) && ok;
            return ok;
        });

        waitProjectLoaded ("Esperar la carga asincrona del proyecto", 3);
        waitMs (3000); // editors open asynchronously; plugins settle

        action ("Comparar con lo guardado (sin tocar nada)", [compareWithExpected, fromRecent]
        {
            return compareWithExpected (fromRecent ? "recientes" : "linea de comandos");
        });

        action ("Proyecto limpio tras abrir", [this, &pc]
        {
            auto ok = check (! pc.isDirty() && ! window.getName().endsWith ("*") && window.getName().contains ("prueba"),
                             "sin cambios: \"" + window.getName() + "\"");
            // The only expected warning: the route from a port that does not exist.
            ok = check (pc.getLastWarnings().size() == 1 && pc.getLastWarnings()[0].contains (kGhostPort),
                        "advertencias de carga: " + pc.getLastWarnings().joinIntoString (" | "))
                 && ok;
            ok = check (window.getProjectInfoText().contains ("prueba"), "linea de proyecto: " + window.getProjectInfoText()) && ok;
            return ok;
        });

        action ("sforzando: instrumento SFZ y ajustes restaurados", [this, idByPlugin]
        {
            juce::MemoryBlock state;
            graph.findInstance (idByPlugin ("sforzando"))->getPlugin()->getStateInformation (state);
            const auto aria = readAriaXml (state);
            auto ok = check (aria.contains ("seno-de-prueba.sfz"), "el estado de sforzando tiene cargado seno-de-prueba.sfz");
            ok = check (aria.contains ("globalTuning=\"7\""), "globalTuning=7") && ok;
            return ok;
        });

        // Same notification path as an edit in the plugin's own editor. Surge's macro M5 has no
        // modulation assigned here, so the level comparison below is not affected.
        action ("Mover un parametro de Surge XT (como desde su editor)", [this, &pc, idByPlugin]
        {
            auto ok = check (! pc.isDirty(), "antes: sin cambios");
            auto* p = graph.findInstance (idByPlugin ("Surge XT"))->getPlugin()->getParameters()[4]; // "M5: -"
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getValue() < 0.5f ? 0.6f : 0.3f);
            p->endChangeGesture();
            return ok;
        });

        waitMs (500);

        action ("El cambio de parametro marca el proyecto como modificado", [this, &pc]
        {
            return check (pc.isDirty() && window.getName().endsWith ("*"), "\"" + window.getName() + "\"");
        });

        action ("Quitar el mute de sforzando para medir", [this, idByPlugin]
        {
            graph.setMute (idByPlugin ("sforzando"), false);
            return true;
        });

        addPlayAndMeasure ("tras reabrir");

        action ("Mismo nivel que al guardar (nota A4, tolerancia 3 dB)", [this, &s, idByPlugin]
        {
            auto ok = true;

            for (auto& e : *s.expected["instances"].getArray())
            {
                const auto id = e["id"].toString();
                const auto before = (float) e["peak"];
                const auto now = s.measuredPeaks[id];
                const auto toDb = [] (float g) { return g > 0.0f ? juce::Decibels::gainToDecibels (g) : -100.0f; };
                ok = check (before > 0.0f && now > 0.0f && std::abs (toDb (before) - toDb (now)) <= 3.0f,
                            e["name"].toString() + ": " + juce::String (toDb (now), 1) + " dBFS (al guardar " + juce::String (toDb (before), 1) + " dBFS)")
                     && ok;
            }

            graph.setMute (idByPlugin ("sforzando"), true);
            return ok;
        });

        return;
    }

    //==========================================================================
    if (projectPhase == "extras")
    {
        const auto extrasFile = dir.getChildFile (kExtrasFileName);
        const auto resavedFile = dir.getChildFile (kResavedFileName);

        action ("Escribir " + extrasFile.getFileName() + ": plugin inexistente, campos desconocidos, params", [this, projectFile, extrasFile, &s, loadExpected]
        {
            auto ok = loadExpected();
            auto root = juce::JSON::parse (projectFile.loadFileAsString());
            auto* rootObject = root.getDynamicObject();

            if (! check (rootObject != nullptr, "lee " + projectFile.getFileName()))
                return false;

            rootObject->setProperty ("name", "Extras");
            rootObject->setProperty ("futureField", juce::JSON::parse (R"({ "tempo": 120.5, "lista": [1, "dos", null, true] })"));
            rootObject->setProperty ("version", 2);
            root["audio"].getDynamicObject()->setProperty ("clockSource", "interno");

            auto* instances = root["instances"].getArray();

            // Dexed: `params` override with a value that differs from its saved state.
            for (auto& instance : *instances)
            {
                instance.getDynamicObject()->setProperty ("color", "#3366ff");
                instance["plugin"].getDynamicObject()->setProperty ("sha256", "0123abcd");

                if (instance["plugin"]["name"] == juce::var ("Dexed"))
                {
                    const auto* e = findById (s.expected["instances"], instance["id"].toString());
                    s.dexedOverrideParam = e != nullptr ? (*e)["changed"][0]["name"].toString() : juce::String();
                    auto params = juce::DynamicObject::Ptr (new juce::DynamicObject());
                    params->setProperty (s.dexedOverrideParam, 0.25);
                    params->setProperty ("Parametro que no existe", 0.5);
                    instance.getDynamicObject()->setProperty ("params", juce::var (params.get()));
                }
            }

            const auto missing = juce::JSON::parse (juce::String::fromUTF8 (R"({
                "id": "0badf00d-0000-4000-8000-00000000abcd",
                "name": "Instrumento desinstalado",
                "plugin": { "name": "PluginQueNoExiste", "manufacturer": "Nadie", "format": "VST3",
                            "fileOrIdentifier": "C:\\Program Files\\Common Files\\VST3\\PluginQueNoExiste.vst3",
                            "uniqueId": "0badf00d", "version": "9.9", "sha256": "ffff" },
                "gainDb": -7.5, "mute": false,
                "state": "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4v",
                "params": { "Cutoff": 0.75 },
                "window": { "x": 333, "y": 222, "open": true, "monitor": 2 },
                "effects": [ { "type": "reverb", "mix": 0.3 } ]
            })"));
            instances->add (missing);

            auto routeExtra = juce::JSON::parse (R"({ "inputDevice": "loopMIDI Port", "channels": [5, 6], "instanceId": "0badf00d-0000-4000-8000-00000000abcd",
                                                     "channelMode": { "remapTo": 2 }, "velocityCurve": "suave" })");
            root["midiRoutes"].getArray()->add (routeExtra);

            ok = check (s.dexedOverrideParam.isNotEmpty(), "override de Dexed: \"" + s.dexedOverrideParam + "\" = 0.25") && ok;
            ok = check (extrasFile.replaceWithText (juce::JSON::toString (root)), "escrito " + extrasFile.getFullPathName()) && ok;
            return ok;
        });

        action ("Abrir el proyecto (Abrir... sin dialogo)", [this, extrasFile, &s]
        {
            s.messagesBefore = s.messages.size();
            window.openProject (extrasFile);
            return true;
        });

        waitProjectLoaded ("Esperar la carga (3 plugins + 1 faltante)", 4);
        waitMs (3000);

        action ("Plugin faltante: abre igual, avisa, fila en rojo", [this, &pc, &s, &panel]
        {
            auto ok = check (pc.getProjectName() == "Extras" && ! pc.isDirty(), "proyecto \"" + pc.getProjectName() + "\" sin cambios");
            s.missingId = "0badf00d-0000-4000-8000-00000000abcd";
            auto* missing = graph.findInstance (s.missingId);
            ok = check (missing != nullptr && missing->state == Instance::State::missing && missing->name == "Instrumento desinstalado",
                        "instancia faltante en la lista: " + (missing != nullptr ? missing->name + " - " + missing->errorMessage : juce::String ("NO")))
                 && ok;
            ok = check (std::abs (missing->gainDb - (-7.5f)) < 1.0e-6f, "ganancia de la faltante -7.5 dB") && ok;

            const auto row = panel.getRowInfo (s.missingId);
            ok = check (row.exists && row.shownAsMissing && row.stateText.contains ("FALTA"), "fila en rojo: \"" + row.stateText + "\"") && ok;

            const auto warnings = pc.getLastWarnings().joinIntoString (" | ");
            ok = check (warnings.contains (kMissingPluginName), "advertencia: " + warnings) && ok;
            ok = check (warnings.contains ("Parametro que no existe"), "advertencia por el parametro inexistente") && ok;

            auto shown = false;
            for (int i = s.messagesBefore; i < s.messages.size(); ++i)
                shown = shown || s.messages[i].contains (kMissingPluginName);

            ok = check (shown, "el aviso se mostro (mensaje de la ventana, en modo prueba sin dialogo)") && ok;

            // The other three are the saved ones; the route to the missing instance is there.
            ok = check (graph.getInstances().size() == 4 && router.getRoutes().size() == 5, "4 instancias, 5 rutas") && ok;
            return ok;
        });

        action ("Override `params` aplicado despues del estado", [this, &s]
        {
            for (auto& instance : graph.getInstances())
                if (instance->description.name == "Dexed")
                    for (auto* p : instance->getPlugin()->getParameters())
                        if (p->getName (256) == s.dexedOverrideParam)
                            return check (std::abs (p->getValue() - 0.25f) < 0.01f,
                                          "Dexed \"" + s.dexedOverrideParam + "\" = " + juce::String (p->getValue(), 4) + " (estado guardado: otro valor)");

            return check (false, "no se encontro el parametro");
        });

        action ("Cambio de ganancia: proyecto modificado, titulo con *", [this, &pc, idByPlugin]
        {
            graph.setGainDb (idByPlugin ("Surge XT"), -20.0f);
            return check (pc.isDirty(), "isDirty");
        });

        waitMs (500);

        action ("Titulo", [this]
        {
            return check (window.getName().endsWith ("*") && window.getProjectInfoText().contains ("*"), "\"" + window.getName() + "\"");
        });

        action ("Autoguardado (intervalo de prueba: 1 s)", [&pc]
        {
            pc.setAutosaveIntervalSeconds (1);
            return true;
        });

        waitFor ("Esperar el autoguardado", [&pc] { return pc.getNumAutosaves() > 0; }, 10.0);

        action ("Contenido del autoguardado", [this, &pc]
        {
            pc.setAutosaveIntervalSeconds (ProjectController::kDefaultAutosaveSeconds);
            const auto file = pc.getAutosaveFile();
            project::Project p;
            auto ok = check (file.existsAsFile() && project::serializer::loadFile (file, p).wasOk(), "autoguardado en " + file.getFullPathName());
            ok = check (p.instances.size() == 4 && p.name == "Extras", juce::String ((int) p.instances.size()) + " instancias") && ok;
            ok = check (pc.isDirty() && pc.getProjectFile().getFileName() == kExtrasFileName, "el autoguardado no cambia el archivo ni limpia el *") && ok;
            return ok;
        });

        action ("\"Cambios sin guardar\" -> Cancelar (Nuevo no hace nada)", [this, &pc, &s]
        {
            s.promptsBefore = window.getNumSavePrompts();
            window.savePromptOverride = [] { return MainWindow::SaveChoice::cancel; };
            window.newProject();
            window.savePromptOverride = [] { return MainWindow::SaveChoice::discard; };
            auto ok = check (window.getNumSavePrompts() == s.promptsBefore + 1, "se pregunto");
            ok = check (graph.getInstances().size() == 4 && pc.isDirty() && pc.getProjectName() == "Extras", "el proyecto sigue abierto y modificado") && ok;
            return ok;
        });

        action ("Guardar como " + resavedFile.getFileName(), [this, &pc, resavedFile]
        {
            const auto result = pc.saveAs (resavedFile);
            auto ok = check (result.wasOk() && ! pc.isDirty(), "guardado " + result.getErrorMessage());
            ok = check (! pc.getAutosaveFile().existsAsFile(), "el autoguardado se borra al guardar") && ok;
            return ok;
        });

        action ("Re-guardado: faltante intacta y campos desconocidos conservados", [this, extrasFile, resavedFile, &s]
        {
            const auto original = juce::JSON::parse (extrasFile.loadFileAsString());
            const auto resaved = juce::JSON::parse (resavedFile.loadFileAsString());
            auto ok = true;

            const auto same = [] (const juce::var& a, const juce::var& b) { return juce::JSON::toString (a, true) == juce::JSON::toString (b, true); };

            ok = check (same (original["futureField"], resaved["futureField"]), "raiz.futureField") && ok;
            ok = check ((int) resaved["version"] == 2, "version 2 conservada") && ok;
            ok = check (resaved["audio"]["clockSource"] == juce::var ("interno"), "audio.clockSource") && ok;

            const auto* om = findById (original["instances"], s.missingId);
            const auto* rm = findById (resaved["instances"], s.missingId);

            if (! check (om != nullptr && rm != nullptr, "la instancia faltante sigue en el archivo"))
                return false;

            ok = check (same ((*om)["plugin"], (*rm)["plugin"]), "faltante: bloque plugin identico (" + juce::JSON::toString ((*rm)["plugin"], true) + ")") && ok;
            ok = check ((*om)["state"] == (*rm)["state"], "faltante: estado base64 identico") && ok;
            ok = check (same ((*om)["params"], (*rm)["params"]), "faltante: params") && ok;
            ok = check (same ((*om)["window"], (*rm)["window"]), "faltante: ventana (con \"monitor\")" + juce::JSON::toString ((*rm)["window"], true)) && ok;
            ok = check (same ((*om)["effects"], (*rm)["effects"]), "faltante: effects") && ok;
            ok = check ((double) (*rm)["gainDb"] == -7.5 && (*rm)["name"] == juce::var ("Instrumento desinstalado"), "faltante: nombre y ganancia") && ok;

            auto colors = 0, hashes = 0;
            for (auto& i : *resaved["instances"].getArray())
            {
                colors += i["color"] == juce::var ("#3366ff") ? 1 : 0;
                hashes += i["plugin"]["sha256"].isString() ? 1 : 0;
            }

            ok = check (colors == 3 && hashes == 4, "instancia.color en las 3 cargadas, plugin.sha256 en las 4") && ok;

            auto foundRoute = false;
            for (auto& r : *resaved["midiRoutes"].getArray())
                if (r["instanceId"].toString() == s.missingId)
                    foundRoute = r["velocityCurve"] == juce::var ("suave") && (int) r["channelMode"]["remapTo"] == 2 && r["channels"].size() == 2;

            ok = check (foundRoute, "ruta a la faltante con velocityCurve y remapTo 2") && ok;

            // The Dexed override stays, following the current value.
            for (auto& i : *resaved["instances"].getArray())
                if (i["plugin"]["name"] == juce::var ("Dexed"))
                    ok = check (std::abs ((double) i["params"][juce::Identifier (s.dexedOverrideParam)] - 0.25) < 0.01
                                    && i["params"].hasProperty ("Parametro que no existe"),
                                "Dexed params conservados: " + juce::JSON::toString (i["params"], true))
                         && ok;

            return ok;
        });

        action ("\"Cambios sin guardar\" -> Guardar (y luego Nuevo)", [this, &pc, &s, resavedFile, idByPlugin]
        {
            graph.setGainDb (idByPlugin ("Dexed"), -12.25f);
            window.savePromptOverride = [] { return MainWindow::SaveChoice::save; };
            s.promptsBefore = window.getNumSavePrompts();
            window.newProject();
            window.savePromptOverride = [] { return MainWindow::SaveChoice::discard; };

            auto ok = check (window.getNumSavePrompts() == s.promptsBefore + 1, "se pregunto");
            ok = check (graph.getInstances().empty() && pc.getProjectName() == juce::String::fromUTF8 ("Sin título") && ! pc.hasFile(),
                        juce::String::fromUTF8 ("proyecto nuevo \"Sin título\""))
                 && ok;

            project::Project saved;
            project::serializer::loadFile (resavedFile, saved);
            auto gain = 0.0;
            for (auto& i : saved.instances)
                if (i.plugin.name == "Dexed")
                    gain = i.gainDb;

            ok = check (std::abs (gain - (-12.25)) < 1.0e-6, "se guardo antes de cerrar (Dexed -12.25 dB en el archivo)") && ok;
            return ok;
        });

        action ("Archivo invalido: error y el proyecto actual no cambia", [this, &pc, dir, &s, findPlugins, &panel]
        {
            findPlugins();
            panel.addInstance (s.dexed);
            const auto bad = dir.getChildFile ("roto.vstproj.json");
            bad.replaceWithText ("{ \"version\": 1, \"instances\": [ ");
            s.messagesBefore = s.messages.size();
            window.openProject (bad); // dirty: the override answers "No guardar" first
            auto ok = check (s.messages.size() == s.messagesBefore + 1 && s.messages.getReference (s.messagesBefore).contains ("JSON"),
                             "error informado: " + s.messages[s.messagesBefore].replace ("\n", " "));
            ok = check (graph.getInstances().size() == 1 && ! pc.hasFile(), "sigue el proyecto anterior") && ok;
            return ok;
        });

        waitFor ("Esperar a Dexed", [this] { return graph.getNumLoading() == 0; }, kLoadTimeoutSeconds);
        return;
    }

    if (projectPhase == "open")
    {
        action ("Proyecto de la linea de comandos", [this, &pc]
        {
            return check (projectFromCommandLine.existsAsFile() && pc.getProjectFile() == projectFromCommandLine,
                          "abierto: " + pc.getProjectFile().getFullPathName());
        });

        waitFor ("Esperar la carga", [this, &pc] { return ! pc.isLoading() && graph.getNumLoading() == 0; }, kLoadTimeoutSeconds);
        waitMs (1500);

        action ("Instancias, rutas y advertencias", [this, &pc]
        {
            for (auto& instance : graph.getInstances())
                cli::print ("  " + instance->name + " (" + instance->description.name + "): "
                            + (instance->isReady() ? juce::String ("cargado")
                                                   : instance->state == Instance::State::missing ? "FALTA - " + instance->errorMessage
                                                                                                 : "ERROR - " + instance->errorMessage)
                            + ", " + juce::String (instance->gainDb, 1) + " dB" + (instance->mute ? ", mute" : ""));

            for (auto& r : router.getRoutes())
                cli::print ("  ruta: " + r.inputDevice + " canales " + MidiRoute::describeChannels (r.channelMask) + " -> "
                            + (graph.findInstance (r.instanceId) != nullptr ? graph.findInstance (r.instanceId)->name : r.instanceId)
                            + (r.isKeep() ? juce::String (" (keep)") : " (remapTo " + juce::String (r.remapChannel) + ")"));

            for (auto& w : pc.getLastWarnings())
                cli::print ("  advertencia: " + w);

            cli::print ("  titulo: " + window.getName());
            return check (! pc.isDirty(), "sin cambios tras abrir");
        });

        return;
    }

    action ("Fase", [this] { return check (false, "fase desconocida \"" + projectPhase + "\" (save | verify-cli | verify-recent | extras)"); });
}

} // namespace vstplayer
