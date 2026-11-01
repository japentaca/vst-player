// "--midi-selftest": stage 4 checks (MIDI input ports and routing). See InstanceSelfTest.h.
#include "app/InstanceSelfTest.h"

#include "app/CommandLine.h"
#include "app/MainWindow.h"
#include "app/ProcessTools.h"
#include "engine/AudioEngine.h"
#include "engine/GainProcessor.h"
#include "engine/InstanceGraph.h"
#include "engine/MidiRouter.h"
#include "engine/MidiSourceProcessor.h"
#include "engine/PluginManager.h"
#include "ui/InstancePanel.h"
#include "ui/MidiPortsWindow.h"

#include <array>
#include <tuple>
#include <map>
#include <memory>

namespace vstplayer
{

namespace
{
constexpr float kTestGainDb = -30.0f;
constexpr double kLoadTimeoutSeconds = 90.0;
constexpr float kSilence = 1.0e-4f; // -80 dBFS

juce::String toDb (float peak)
{
    return peak > 0.0f ? juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS" : juce::String ("silencio");
}

juce::uint16 maskOf (std::initializer_list<int> channels)
{
    juce::uint16 mask = 0;

    for (auto c : channels)
        mask = (juce::uint16) (mask | (1u << (c - 1)));

    return mask;
}

// "9X:NN:VV" for a note-on / "8X:NN:00" for a note-off (channel 1..16).
juce::String noteOnHex (int channel, int note, int velocity = 100)
{
    return juce::String::toHexString (0x90 + channel - 1) + ":" + juce::String::toHexString (note) + ":" + juce::String::toHexString (velocity);
}

juce::String noteOffHex (int channel, int note)
{
    return juce::String::toHexString (0x80 + channel - 1) + ":" + juce::String::toHexString (note) + ":00";
}

struct Counts
{
    std::array<int, 16> ons {}, offs {};
    juce::int64 events = 0;
};

struct MidiTestState
{
    juce::PluginDescription dexed, surge;
    InstanceId a, b, c; // A = Dexed, B = Surge XT, C = Dexed (second instance)
    juce::String loopIn, loopOut;

    std::unique_ptr<juce::ChildProcess> child;
    std::map<InstanceId, Counts> snapshot;
    juce::uint64 logSerialBefore = 0;

    juce::String loopMidiPath;

    juce::String originalType;
    juce::AudioDeviceManager::AudioDeviceSetup originalSetup;
};

MidiTestState& midiState()
{
    static MidiTestState s;
    return s;
}
} // namespace

//==============================================================================
void InstanceSelfTest::buildMidiSteps()
{
    auto& panel = window.getInstancePanel();
    auto& s = midiState();

    const auto nameOf = [&s] (const InstanceId& id) -> juce::String
    {
        return id == s.a ? "A (Dexed)" : id == s.b ? "B (Surge XT)" : id == s.c ? "C (Dexed 2)" : "?";
    };

    const auto countsOf = [this] (const InstanceId& id)
    {
        Counts counts;

        if (auto* instance = graph.findInstance (id))
        {
            if (auto* midi = instance->getMidiSource())
            {
                for (int ch = 1; ch <= 16; ++ch)
                {
                    counts.ons[(size_t) ch - 1] = midi->getNoteOnsDelivered (ch);
                    counts.offs[(size_t) ch - 1] = midi->getNoteOffsDelivered (ch);
                }

                counts.events = midi->getEventsDelivered();
            }
        }

        return counts;
    };

    const auto peakOf = [this] (const InstanceId& id)
    {
        auto* instance = graph.findInstance (id);
        auto* gain = instance != nullptr ? instance->getGainProcessor() : nullptr;
        return gain != nullptr ? gain->getAndResetPeak() : -1.0f;
    };

    const auto ids = [&s] { return std::vector<InstanceId> { s.a, s.b, s.c }; };

    // Snapshot of counters + meters reset (start of a measurement).
    const auto takeSnapshot = [this, &s, countsOf, peakOf, ids]
    {
        for (auto& id : ids())
        {
            s.snapshot[id] = countsOf (id);
            peakOf (id);
        }

        s.logSerialBefore = router.getLogSerial();
    };

    const auto delta = [&s, countsOf] (const InstanceId& id)
    {
        auto now = countsOf (id);
        const auto& before = s.snapshot[id];

        for (size_t i = 0; i < 16; ++i)
        {
            now.ons[i] -= before.ons[i];
            now.offs[i] -= before.offs[i];
        }

        now.events -= before.events;
        return now;
    };

    const auto describeCounts = [] (const Counts& d)
    {
        juce::StringArray parts;

        for (size_t i = 0; i < 16; ++i)
            if (d.ons[i] != 0 || d.offs[i] != 0)
                parts.add ("ch" + juce::String ((int) i + 1) + " on+" + juce::String (d.ons[i]) + " off+" + juce::String (d.offs[i]));

        return parts.isEmpty() ? juce::String ("nada") : parts.joinIntoString (", ");
    };

    // Sends MIDI from a separate process (like Chrome / carrilion would) to the loopMIDI output.
    const auto sendExternal = [this, &s] (const juce::String& label, const juce::StringArray& messages)
    {
        action ("Enviar al puerto loopMIDI desde otro proceso: " + label, [&s, messages]
        {
            juce::StringArray command;
            command.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName());
            command.add ("--midi-send");
            command.add (s.loopOut);
            command.addArray (messages);

            s.child = std::make_unique<juce::ChildProcess>();
            return s.child->start (command, juce::ChildProcess::wantStdOut);
        });

        waitFor ("(esperar al emisor)", [this, &s]
        {
            if (s.child == nullptr)
                return true;

            if (s.child->isRunning())
                return false;

            const auto exitCode = s.child->getExitCode();
            const auto output = s.child->readAllProcessOutput().trim();
            s.child = nullptr;

            if (exitCode != 0)
            {
                ++failures;
                cli::print ("  FALLO emisor exit " + juce::String ((int) exitCode) + ": " + output);
            }

            return true;
        }, 20.0);
    };

    // Expects exactly the given note-on deliveries (instance -> output channel) and nothing else.
    const auto expectOnly = [this, ids, delta, describeCounts, nameOf] (const juce::String& label,
                                                                        std::vector<std::pair<const InstanceId*, int>> expected)
    {
        action (label, [this, ids, delta, describeCounts, nameOf, expected]
        {
            auto ok = true;

            for (auto& id : ids())
            {
                const auto d = delta (id);
                std::array<int, 16> want {};

                for (auto& [target, channel] : expected)
                    if (*target == id)
                        ++want[(size_t) channel - 1];

                ok = check (d.ons == want, nameOf (id) + ": " + describeCounts (d)) && ok;
            }

            return ok;
        });
    };

    const auto expectSignal = [this, peakOf, nameOf] (const juce::String& label, std::vector<const InstanceId*> sounding)
    {
        action (label, [this, peakOf, nameOf, sounding]
        {
            auto ok = true;

            for (auto* id : sounding)
            {
                const auto peak = peakOf (*id);
                ok = check (peak > 0.0f, nameOf (*id) + " suena: pico " + toDb (peak)) && ok;
            }

            return ok;
        });
    };

    const auto expectSilence = [this, peakOf, nameOf, ids] (const juce::String& label, std::vector<const InstanceId*> silent)
    {
        action ("(reiniciar medidores)", [peakOf, ids] { for (auto& id : ids()) peakOf (id); return true; });
        waitMs (500);

        action (label, [this, peakOf, nameOf, silent]
        {
            auto ok = true;

            for (auto* id : silent)
            {
                const auto peak = peakOf (*id);
                ok = check (peak >= 0.0f && peak < kSilence, nameOf (*id) + " en silencio: pico " + toDb (peak)) && ok;
            }

            return ok;
        });
    };

    // One full routing case: note-on on `channel`, check who got it, note-off.
    const auto routeCase = [this, takeSnapshot, sendExternal, expectOnly, expectSignal]
                           (const juce::String& label, int channel, std::vector<std::pair<const InstanceId*, int>> expected)
    {
        action ("== " + label, [takeSnapshot] { takeSnapshot(); return true; });
        sendExternal ("note-on canal " + juce::String (channel), { noteOnHex (channel, 60) });
        waitMs (500);
        expectOnly (label + ": entregas", expected);

        std::vector<const InstanceId*> sounding;
        for (auto& e : expected)
            sounding.push_back (e.first);

        if (! sounding.empty())
            expectSignal (label + ": senal", sounding);

        sendExternal ("note-off canal " + juce::String (channel), { noteOffHex (channel, 60) });
        waitMs (700);
    };

    //==========================================================================
    action ("Puertos MIDI del sistema", [this, &s]
    {
        const auto inputs = juce::MidiInput::getAvailableDevices();
        const auto outputs = juce::MidiOutput::getAvailableDevices();

        for (auto& d : inputs)
            cli::print ("  entrada: " + d.name);

        for (auto& d : outputs)
            cli::print ("  salida:  " + d.name);

        for (auto& in : inputs)
            for (auto& out : outputs)
                if (s.loopIn.isEmpty() && in.name.containsIgnoreCase ("loopMIDI") && in.name == out.name)
                {
                    s.loopIn = in.name;
                    s.loopOut = out.name;
                }

        return check (s.loopIn.isNotEmpty(), "puerto loopMIDI (entrada y salida): " + (s.loopIn.isNotEmpty() ? s.loopIn : juce::String ("NINGUNO")));
    });

    action ("Buscar Dexed y Surge XT", [this, &s]
    {
        auto ok = true;

        for (auto [name, target] : { std::pair { "Dexed", &s.dexed }, std::pair { "Surge XT", &s.surge } })
        {
            auto found = false;

            for (auto& d : plugins.getKnownPluginList().getTypes())
                if (d.name == name && d.isInstrument)
                {
                    *target = d;
                    found = true;
                    break;
                }

            ok = check (found, juce::String (name)) && ok;
        }

        return ok;
    });

    action ("Agregar A = Dexed, B = Surge XT, C = Dexed", [&panel, &s]
    {
        s.a = panel.addInstance (s.dexed);
        s.b = panel.addInstance (s.surge);
        s.c = panel.addInstance (s.dexed);
        return true;
    });

    waitFor ("Esperar las cargas", [this, ids]
    {
        for (auto& id : ids())
        {
            auto* instance = graph.findInstance (id);
            if (instance == nullptr || instance->state == Instance::State::loading)
                return false;
        }

        return true;
    }, kLoadTimeoutSeconds);

    action ("Estado de carga", [this, ids, nameOf]
    {
        auto ok = true;

        for (auto& id : ids())
        {
            auto* instance = graph.findInstance (id);
            ok = check (instance != nullptr && instance->isReady(), nameOf (id) + (instance != nullptr ? " " + instance->errorMessage : juce::String())) && ok;
            graph.setGainDb (id, kTestGainDb);
        }

        return ok;
    });

    //==========================================================================
    action ("Rutas por la UI: A <- canal 1 keep; B <- canales 2,3 remapTo 1; C <- canal 4 keep", [this, &panel, &s, nameOf]
    {
        panel.setInstanceMidiPort (s.a, s.loopIn);
        panel.setInstanceMidiChannels (s.a, maskOf ({ 1 }));

        panel.setInstanceMidiPort (s.b, s.loopIn);
        panel.setInstanceMidiChannels (s.b, maskOf ({ 2, 3 }));
        panel.setInstanceMidiRemap (s.b, 1);

        panel.setInstanceMidiPort (s.c, s.loopIn);
        panel.setInstanceMidiChannels (s.c, maskOf ({ 4 }));
        panel.setInstanceMidiRemap (s.c, 0);

        for (auto& route : router.getRoutes())
            cli::print ("  ruta: " + route.inputDevice + " canales [" + MidiRoute::describeChannels (route.channelMask) + "] -> "
                        + nameOf (route.instanceId) + (route.isKeep() ? " keep" : " remapTo " + juce::String (route.remapChannel)));

        return check (router.getRoutes().size() == 3, "3 rutas") && check (router.isPortOpen (s.loopIn), s.loopIn + " abierto");
    });

    waitMs (200); // the rows refresh asynchronously (router change message)

    action ("Las filas muestran las rutas", [&panel, &s, nameOf, this]
    {
        auto ok = true;

        for (auto [id, channels, mode] : { std::tuple { s.a, "Canales: 1", "keep" }, std::tuple { s.b, "Canales: 2, 3", "canal 1" },
                                           std::tuple { s.c, "Canales: 4", "keep" } })
        {
            const auto info = panel.getRowInfo (id);
            ok = check (info.exists && info.port == s.loopIn && info.channels == channels && info.mode.contains (mode),
                        nameOf (id) + ": [" + info.port + "] [" + info.channels + "] [" + info.mode + "]")
                 && ok;
        }

        return ok;
    });

    //==========================================================================
    routeCase ("Canal 1 -> solo A (keep, canal 1)", 1, { { &s.a, 1 } });
    routeCase ("Canal 2 -> solo B (remapTo 1)", 2, { { &s.b, 1 } });
    routeCase ("Canal 3 -> solo B (remapTo 1)", 3, { { &s.b, 1 } });
    routeCase ("Canal 4 -> solo C (keep, canal 4)", 4, { { &s.c, 4 } });
    routeCase ("Canal 5 -> nadie (sin ruta)", 5, {});

    action ("El log del router registra el canal 5 como 'sin ruta'", [this, &s]
    {
        auto found = false;

        for (auto& entry : router.getRecentMessages())
            if (entry.serial > s.logSerialBefore && entry.message.isNoteOn() && entry.message.getChannel() == 5)
                found = entry.numTargets == 0 && entry.device == s.loopIn;

        return check (found, "note-on canal 5 recibido en " + s.loopIn + " sin destino");
    });

    action ("B pasa a keep (UI)", [&panel, &s] { panel.setInstanceMidiRemap (s.b, 0); return true; });
    routeCase ("Canal 2 -> solo B (keep, canal 2)", 2, { { &s.b, 2 } });
    routeCase ("Canal 3 -> solo B (keep, canal 3)", 3, { { &s.b, 3 } });

    action ("C escucha canales 1 y 4", [&panel, &s] { panel.setInstanceMidiChannels (s.c, maskOf ({ 1, 4 })); return true; });
    routeCase ("Canal 1 -> A y C a la vez", 1, { { &s.a, 1 }, { &s.c, 1 } });
    action ("C vuelve a canal 4; B vuelve a remapTo 1", [&panel, &s]
    {
        panel.setInstanceMidiChannels (s.c, maskOf ({ 4 }));
        panel.setInstanceMidiRemap (s.b, 1);
        return true;
    });

    action ("LED de actividad MIDI", [&panel, &s, nameOf, this]
    {
        auto ok = true;

        for (auto& id : { s.a, s.b, s.c })
        {
            const auto info = panel.getRowInfo (id);
            ok = check (info.ledFlashes > 0 && router.getActivityCount (id) > 0,
                        nameOf (id) + ": LED encendido " + juce::String (info.ledFlashes) + " veces, mensajes ruteados "
                            + juce::String (router.getActivityCount (id)))
                 && ok;
        }

        return ok;
    });

    //==========================================================================
    // Retrigger (carrilion piano mode 'retrigger': note-off, then note-on 20 ms later).
    action ("== Retrigger estilo carrilion en A (on, off, +20 ms, on, +300 ms, off)", [takeSnapshot] { takeSnapshot(); return true; });
    sendExternal ("retrigger", { noteOnHex (1, 62), "+150", noteOffHex (1, 62), "+20", noteOnHex (1, 62), "+300", noteOffHex (1, 62) });
    waitMs (300);

    action ("Retrigger: 2 note-on y 2 note-off entregados a A", [this, &s, delta, describeCounts]
    {
        const auto d = delta (s.a);
        return check (d.ons[0] == 2 && d.offs[0] == 2, "A: " + describeCounts (d));
    });

    waitMs (2500);
    expectSilence ("Retrigger: A sin notas colgadas", { &s.a });

    action ("== Note-on repetido sin note-off en A (on, +100 ms, on, +300 ms, off)", [takeSnapshot] { takeSnapshot(); return true; });
    sendExternal ("doble note-on", { noteOnHex (1, 64), "+100", noteOnHex (1, 64), "+300", noteOffHex (1, 64) });
    waitMs (300);

    action ("El router agrega el note-off que falta antes del segundo note-on", [this, &s, delta, describeCounts]
    {
        const auto d = delta (s.a);
        return check (d.ons[0] == 2 && d.offs[0] == 2, "A: " + describeCounts (d) + " (1 note-off agregado + 1 de la fuente)");
    });

    waitMs (2500);
    expectSilence ("Note-on repetido: A sin notas colgadas", { &s.a });

    action ("== Dos canales remapeados al mismo canal (B: ch2 y ch3 la misma nota, un solo note-off)", [takeSnapshot] { takeSnapshot(); return true; });
    sendExternal ("ch2 on, ch3 on, ch3 off", { noteOnHex (2, 67), "+100", noteOnHex (3, 67), "+300", noteOffHex (3, 67) });
    waitMs (2800);
    expectSilence ("Remap compartido: B sin notas colgadas", { &s.b });
    sendExternal ("ch2 off (llega tarde, inofensivo)", { noteOffHex (2, 67) });

    //==========================================================================
    action ("== Nota sostenida en A y cambio de ruta (A pasa a canal 6)", [takeSnapshot] { takeSnapshot(); return true; });
    sendExternal ("note-on canal 1 sin note-off", { noteOnHex (1, 65) });
    waitMs (400);
    expectSignal ("A suena con la nota sostenida", { &s.a });
    action ("Cambiar A a canal 6 con la nota apretada", [&panel, &s] { panel.setInstanceMidiChannels (s.a, maskOf ({ 6 })); return true; });
    waitMs (200);

    action ("El router suelta la nota de A al cambiar la ruta", [this, &s, delta, describeCounts]
    {
        const auto d = delta (s.a);
        return check (d.offs[0] == 1, "A: " + describeCounts (d));
    });

    waitMs (2500);
    expectSilence ("A en silencio sin el note-off de la fuente", { &s.a });
    sendExternal ("note-off tardio canal 1", { noteOffHex (1, 65) });
    action ("A vuelve a canal 1", [&panel, &s] { panel.setInstanceMidiChannels (s.a, maskOf ({ 1 })); return true; });

    //==========================================================================
    action ("== Panico con notas sostenidas desde el puerto en A y C", [takeSnapshot] { takeSnapshot(); return true; });
    sendExternal ("notas sin note-off en canales 1 y 4", { noteOnHex (1, 60), noteOnHex (1, 67), noteOnHex (4, 60) });
    waitMs (400);
    expectSignal ("A y C suenan", { &s.a, &s.c });
    action ("Boton Panico", [&panel] { panel.panic(); return true; });
    waitMs (1500);
    expectSilence ("Silencio despues del panico", { &s.a, &s.c });

    action ("(snapshot)", [takeSnapshot] { takeSnapshot(); return true; });
    sendExternal ("la misma nota otra vez (el panico olvido las notas sostenidas)", { noteOnHex (1, 60), "+200", noteOffHex (1, 60) });
    waitMs (300);

    action ("Sin note-off de retrigger extra despues del panico", [this, &s, delta, describeCounts]
    {
        const auto d = delta (s.a);
        return check (d.ons[0] == 1 && d.offs[0] == 1, "A: " + describeCounts (d));
    });

    //==========================================================================
    action ("== Mensajes de sistema (clock, active sensing, sysex) se descartan", [takeSnapshot] { takeSnapshot(); return true; });
    sendExternal ("F8, FE, F0 7D 01 02 F7", { "F8", "FE", "F0:7D:01:02:F7", "F8" });
    waitMs (400);

    action ("Ninguna instancia recibio mensajes de sistema", [this, &s, ids, delta, nameOf]
    {
        auto ok = true;

        for (auto& id : ids())
        {
            const auto d = delta (id);
            ok = check (d.events == 0, nameOf (id) + ": eventos entregados " + juce::String (d.events)) && ok;
        }

        int dropped = 0;

        for (auto& entry : router.getRecentMessages())
            if (entry.serial > s.logSerialBefore && entry.dropped)
                ++dropped;

        return check (dropped >= 2, "el log marca " + juce::String (dropped) + " mensajes como descartados") && ok;
    });

    //==========================================================================
    action ("Abrir Audio > Puertos MIDI", [this] { window.showMidiPorts(); return window.getMidiPortsWindow() != nullptr; });
    sendExternal ("una nota para el log", { noteOnHex (4, 72), "+100", noteOffHex (4, 72) });
    waitMs (600);

    action ("La ventana de diagnostico muestra el puerto y el log", [this, &s]
    {
        auto* w = window.getMidiPortsWindow();
        const auto ports = w != nullptr ? w->getPortsText() : juce::String();
        const auto logText = w != nullptr ? w->getLogText() : juce::String();

        for (auto& line : juce::StringArray::fromLines (ports))
            if (line.isNotEmpty())
                cli::print ("  | " + line);

        const auto logLines = juce::StringArray::fromLines (logText.trim());

        for (int i = juce::jmax (0, logLines.size() - 4); i < logLines.size(); ++i)
            cli::print ("  | " + logLines[i]);

        auto ok = check (w != nullptr && w->isVisible(), "ventana visible");
        ok = check (ports.contains (s.loopIn) && ports.contains ("abierto"), "estado del puerto") && ok;
        return check (logText.contains ("Note on") && logText.contains ("instancia"), "log con mensajes ruteados") && ok;
    });

    action ("Cerrar la ventana de diagnostico", [this]
    {
        if (auto* w = window.getMidiPortsWindow())
            w->closeButtonPressed();

        return ! router.isMonitoringAllPorts();
    });

    //==========================================================================
    // The loopMIDI port deleted and created again while routed (opt-in: --restart-loopmidi).
    // loopMIDI.exe is terminated (the driver removes its ports with the process) and started
    // again minimized; it re-creates its ports from its own configuration. Same effect as
    // deleting and re-adding the port in loopMIDI's window, without touching its settings.
    // (A teVirtualMIDI port of our own was tried first: the driver creates it, but with the
    // DLL that loopMIDI installs no data flows through ports of other applications.)
    if (juce::JUCEApplicationBase::getCommandLineParameterArray().contains ("--restart-loopmidi"))
    {
        action ("== Nota sostenida en A antes de cerrar loopMIDI", [takeSnapshot] { takeSnapshot(); return true; });
        sendExternal ("note-on canal 1 sin note-off", { noteOnHex (1, 69) });
        waitMs (400);
        expectSignal ("A suena", { &s.a });

        action ("Cerrar loopMIDI.exe (el puerto desaparece)", [this, &s]
        {
            s.loopMidiPath = juce::String (processtools::findProcessPath (L"loopMIDI.exe").c_str());
            const auto killed = processtools::killProcesses (L"loopMIDI.exe");
            return check (killed > 0 && s.loopMidiPath.isNotEmpty(), "terminado: " + s.loopMidiPath);
        });

        waitFor ("El router detecta la desconexion", [this, &s] { return ! router.isPortOpen (s.loopIn); }, 10.0);

        action ("Nota soltada, ruta conservada y fila en 'desconectado'", [this, &panel, &s, delta, describeCounts]
        {
            const auto d = delta (s.a);
            const auto routes = router.getRoutesForInstance (s.a);
            auto ok = check (d.offs[0] == 1, "A: " + describeCounts (d));
            ok = check (routes.size() == 1 && routes.front().inputDevice == s.loopIn, "la ruta de A sigue apuntando a " + s.loopIn) && ok;

            const auto info = panel.getRowInfo (s.a);
            return check (info.port.contains ("desconectado"), "la fila muestra: " + info.port) && ok;
        });

        waitMs (2200);
        expectSilence ("A en silencio tras la desconexion", { &s.a });

        action ("Volver a abrir loopMIDI (minimizado)", [this, &s]
        {
            return check (processtools::launchMinimized (s.loopMidiPath.toWideCharPointer()), "lanzado " + s.loopMidiPath);
        });

        waitFor ("El router reabre el puerto solo", [this, &s] { return router.isPortOpen (s.loopIn); }, 30.0);
        waitMs (500);

        action ("Fila de A otra vez conectada", [&panel, &s, this]
        {
            const auto info = panel.getRowInfo (s.a);
            return check (info.port == s.loopIn, "la fila muestra: " + info.port);
        });

        routeCase ("Puerto recreado: canal 1 -> solo A", 1, { { &s.a, 1 } });
        routeCase ("Puerto recreado: canal 2 -> solo B (remapTo 1)", 2, { { &s.b, 1 } });
        routeCase ("Puerto recreado: canal 4 -> solo C", 4, { { &s.c, 4 } });
    }
    else
    {
        action ("Borrar y recrear el puerto loopMIDI: OMITIDO (usar --restart-loopmidi)", [] { return true; });
    }

    //==========================================================================
    // Latency reported by the AudioDeviceManager (WASAPI shared only offers 480 here, so the
    // 256 / 128 values come from DirectSound, as in stage 3), plus a MIDI check at 128.
    const auto latencyStep = [this, &s] (int bufferSize)
    {
        action ("DirectSound con buffer " + juce::String (bufferSize), [this, &s, bufferSize]
        {
            auto& dm = engine.getDeviceManager();

            if (s.originalType.isEmpty())
            {
                s.originalType = dm.getCurrentAudioDeviceType();
                s.originalSetup = dm.getAudioDeviceSetup();
            }

            if (dm.getCurrentAudioDeviceType() != "DirectSound")
                dm.setCurrentAudioDeviceType ("DirectSound", true);

            auto setup = dm.getAudioDeviceSetup();
            setup.bufferSize = bufferSize;
            setup.sampleRate = s.originalSetup.sampleRate;
            const auto error = dm.setAudioDeviceSetup (setup, true);
            auto* device = dm.getCurrentAudioDevice();

            if (! check (error.isEmpty() && device != nullptr, "dispositivo abierto " + error))
                return false;

            const auto sr = device->getCurrentSampleRate();
            const auto out = device->getOutputLatencyInSamples();
            const auto in = device->getInputLatencyInSamples();
            const auto buffer = device->getCurrentBufferSizeSamples();
            cli::print ("  LATENCIA " + dm.getCurrentAudioDeviceType() + " / " + device->getName() + ": sr " + juce::String (sr, 0)
                        + ", buffer " + juce::String (buffer) + " (" + juce::String (1000.0 * buffer / sr, 2) + " ms)"
                        + ", getOutputLatencyInSamples " + juce::String (out) + " (" + juce::String (1000.0 * out / sr, 2) + " ms)"
                        + ", getInputLatencyInSamples " + juce::String (in));
            return check (buffer == bufferSize, "buffer " + juce::String (buffer));
        });

        waitMs (800);
    };

    latencyStep (256);
    latencyStep (128);
    routeCase ("Con DirectSound 128: canal 1 -> solo A", 1, { { &s.a, 1 } });

    action ("Restaurar el dispositivo original", [this, &s]
    {
        auto& dm = engine.getDeviceManager();

        if (s.originalType.isNotEmpty() && dm.getCurrentAudioDeviceType() != s.originalType)
            dm.setCurrentAudioDeviceType (s.originalType, true);

        const auto error = dm.setAudioDeviceSetup (s.originalSetup, true);
        auto* device = dm.getCurrentAudioDevice();

        if (device != nullptr)
            cli::print ("  LATENCIA " + dm.getCurrentAudioDeviceType() + " / " + device->getName() + ": buffer "
                        + juce::String (device->getCurrentBufferSizeSamples()) + ", getOutputLatencyInSamples "
                        + juce::String (device->getOutputLatencyInSamples()));

        return check (error.isEmpty() && device != nullptr, "restaurado " + engine.describeDevice());
    });

    waitMs (800);

    //==========================================================================
    action ("Quitar C: sus rutas desaparecen", [this, &panel, &s]
    {
        panel.removeInstance (s.c);
        return check (router.getRoutesForInstance (s.c).empty() && router.getRoutes().size() == 2, "quedan 2 rutas");
    });

    routeCase ("Sin C: canal 4 -> nadie", 4, {});

    action ("Quitar las entradas MIDI de A y B: el puerto se cierra", [this, &panel, &s]
    {
        panel.setInstanceMidiPort (s.a, {});
        panel.setInstanceMidiPort (s.b, {});
        return check (router.getRoutes().empty(), "sin rutas") && check (! router.isPortOpen (s.loopIn), s.loopIn + " cerrado");
    });

    action ("Volver a rutear A y salir con el puerto abierto", [this, &panel, &s]
    {
        panel.setInstanceMidiPort (s.a, s.loopIn);
        return check (router.isPortOpen (s.loopIn), s.loopIn + " abierto");
    });

    sendExternal ("nota final", { noteOnHex (1, 60), "+100", noteOffHex (1, 60) });
    waitMs (300);

    action ("(fin: limpiar estado)", [&s]
    {
        // Never leave loopMIDI closed if the restart above failed half way.
        if (s.loopMidiPath.isNotEmpty() && processtools::findProcessPath (L"loopMIDI.exe").empty())
            processtools::launchMinimized (s.loopMidiPath.toWideCharPointer());

        s.child = nullptr;
        return true;
    });
}

} // namespace vstplayer
