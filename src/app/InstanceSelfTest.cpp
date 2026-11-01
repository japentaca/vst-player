#include "app/InstanceSelfTest.h"

#include "app/CommandLine.h"
#include "app/MainWindow.h"
#include "engine/AudioEngine.h"
#include "engine/GainProcessor.h"
#include "engine/InstanceGraph.h"
#include "engine/MidiSourceProcessor.h"
#include "engine/PluginManager.h"
#include "ui/InstancePanel.h"
#include "ui/PluginEditorWindow.h"

#include <memory>

namespace vstplayer
{

namespace
{
constexpr int kTickMs = 50;
constexpr float kTestGainDb = -30.0f; // keep the speakers quiet while testing
constexpr double kLoadTimeoutSeconds = 90.0;

juce::String toDb (float peak)
{
    return peak > 0.0f ? juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS" : juce::String ("silencio");
}

// State shared by the steps (they are lambdas created up front).
struct TestState
{
    juce::PluginDescription dexed, surge, sforzando;
    InstanceId dexedId, surgeId, sforzandoId;
    juce::Point<int> dexedEditorPosition;

    juce::String originalType;
    juce::AudioDeviceManager::AudioDeviceSetup originalSetup;
    bool deviceChanged = false;
};
} // namespace

static TestState& state()
{
    static TestState s;
    return s;
}

InstanceSelfTest::InstanceSelfTest (MainWindow& w, AudioEngine& e, PluginManager& p, InstanceGraph& g, MidiRouter& r, Kind k,
                                    juce::String single)
    : window (w), engine (e), plugins (p), graph (g), router (r), kind (k), singlePluginName (std::move (single))
{
}

InstanceSelfTest::~InstanceSelfTest()
{
    stopTimer();
    state() = TestState();
    clearProjectTestState();
}

void InstanceSelfTest::start()
{
    cli::print (kind == Kind::midi      ? juce::String ("VstPlayer --midi-selftest")
                : kind == Kind::project ? "VstPlayer --project-selftest " + projectPhase
                                        : juce::String ("VstPlayer --instance-selftest"));
    cli::print (engine.describeDevice());
    if (kind == Kind::project)
        buildProjectSteps();
    else if (kind == Kind::midi)
        buildMidiSteps();
    else if (singlePluginName.isNotEmpty())
        buildSinglePluginSteps();
    else
        buildSteps();

    current = 0;
    stepStarted = false;
    startTimer (kTickMs);
}

//==============================================================================
void InstanceSelfTest::action (const juce::String& name, std::function<bool()> fn)
{
    steps.push_back ({ name, [this, name, fn]
                       {
                           if (! fn())
                           {
                               ++failures;
                               cli::print ("  FALLO en: " + name);
                           }
                           return true;
                       },
                       5.0 });
}

void InstanceSelfTest::waitMs (int ms)
{
    steps.push_back ({ "esperar " + juce::String (ms) + " ms",
                       [this, ms] { return juce::Time::getMillisecondCounterHiRes() - stepStartMs >= ms; },
                       ms / 1000.0 + 5.0 });
}

void InstanceSelfTest::waitFor (const juce::String& name, std::function<bool()> condition, double timeoutSeconds)
{
    steps.push_back ({ name, std::move (condition), timeoutSeconds });
}

bool InstanceSelfTest::check (bool condition, const juce::String& what)
{
    cli::print (juce::String (condition ? "  OK    " : "  FALLO ") + what);
    return condition;
}

void InstanceSelfTest::timerCallback()
{
    if (finished)
        return;

    if (current >= steps.size())
    {
        finish (failures == 0);
        return;
    }

    auto& step = steps[current];

    if (! stepStarted)
    {
        stepStarted = true;
        stepStartMs = juce::Time::getMillisecondCounterHiRes();

        if (! step.name.startsWith ("esperar"))
            cli::print ("[" + juce::String ((int) current + 1) + "/" + juce::String ((int) steps.size()) + "] " + step.name);
    }

    if (step.poll())
    {
        ++current;
        stepStarted = false;
        return;
    }

    const auto elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - stepStartMs) / 1000.0;

    if (elapsedSeconds > step.timeoutSeconds)
    {
        ++failures;
        cli::print ("  FALLO timeout (" + juce::String (step.timeoutSeconds, 0) + " s) en: " + step.name);
        ++current;
        stepStarted = false;
    }
}

void InstanceSelfTest::finish (bool ok)
{
    finished = true;
    stopTimer();
    cli::print (ok ? "RESULTADO: OK" : "RESULTADO: FALLO (" + juce::String (failures) + " fallas)");
    if (kind == Kind::instances)
        cli::print ("Saliendo (el editor de Dexed queda abierto a proposito: prueba el cierre con editores abiertos).");

    juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (ok ? 0 : 1);
    juce::JUCEApplicationBase::getInstance()->systemRequestedQuit();
}

//==============================================================================
void InstanceSelfTest::buildSteps()
{
    auto& panel = window.getInstancePanel();
    auto& keys = panel.getKeyboardState();
    auto& s = state();

    const auto gainOf = [this] (const InstanceId& id) -> GainProcessor*
    {
        auto* instance = graph.findInstance (id);
        return instance != nullptr ? instance->getGainProcessor() : nullptr;
    };

    const auto peakOf = [gainOf] (const InstanceId& id)
    {
        auto* gain = gainOf (id);
        return gain != nullptr ? gain->getAndResetPeak() : -1.0f;
    };

    const auto resetPeaks = [this, peakOf, &s]
    {
        peakOf (s.dexedId);
        peakOf (s.surgeId);
        peakOf (s.sforzandoId);

        if (auto* mixer = engine.getMixer())
            mixer->getAndResetPeak();
    };

    const auto isReady = [this] (const InstanceId& id)
    {
        auto* instance = graph.findInstance (id);
        return instance != nullptr && instance->state != Instance::State::loading;
    };

    const auto reportLoad = [this] (const InstanceId& id, const juce::String& label)
    {
        auto* instance = graph.findInstance (id);

        if (instance == nullptr)
            return check (false, label + ": la instancia no existe");

        if (instance->state == Instance::State::failed)
            return check (false, label + ": " + instance->errorMessage);

        auto* plugin = instance->getPlugin();
        return check (plugin != nullptr,
                      label + " cargado: " + instance->name + " (salidas " + juce::String (plugin->getTotalNumOutputChannels())
                          + ", acepta MIDI " + (plugin->acceptsMidi() ? "si" : "no") + ", editor "
                          + (plugin->hasEditor() ? "propio" : "generico") + ")");
    };

    // Play one note on each instance through the virtual keyboard path and check the signal.
    const auto playBothAndCheck = [this, &panel, &keys, &s, peakOf, resetPeaks] (const juce::String& label)
    {
        action (label + ": notas con el teclado virtual (Dexed C4, Surge XT G4)", [&panel, &keys, &s, resetPeaks]
        {
            resetPeaks();
            panel.selectInstance (s.dexedId);
            keys.noteOn (InstancePanel::kKeyboardChannel, 60, 0.8f);
            panel.selectInstance (s.surgeId);
            keys.noteOn (InstancePanel::kKeyboardChannel, 67, 0.8f);
            return true;
        });

        waitMs (700);

        action (label + ": senal en cada instancia y en la mezcla", [this, &keys, &s, peakOf, label]
        {
            const auto dexedPeak = peakOf (s.dexedId);
            const auto surgePeak = peakOf (s.surgeId);
            const auto mixerPeak = engine.getMixer() != nullptr ? engine.getMixer()->getAndResetPeak() : -1.0f;

            keys.noteOff (InstancePanel::kKeyboardChannel, 60, 0.0f);
            keys.noteOff (InstancePanel::kKeyboardChannel, 67, 0.0f);

            auto ok = check (dexedPeak > 0.0f, "Dexed pico " + toDb (dexedPeak));
            ok = check (surgePeak > 0.0f, "Surge XT pico " + toDb (surgePeak)) && ok;
            ok = check (mixerPeak > 0.0f, "Mezcla (salida) pico " + toDb (mixerPeak)) && ok;
            return ok;
        });

        waitMs (300);
    };

    //--------------------------------------------------------------------------
    action ("Buscar Dexed, Surge XT y sforzando en plugins.xml", [this, &s]
    {
        auto ok = true;

        for (auto [name, target] : { std::pair { "Dexed", &s.dexed }, std::pair { "Surge XT", &s.surge }, std::pair { "sforzando", &s.sforzando } })
        {
            auto found = false;

            for (auto& d : plugins.getKnownPluginList().getTypes())
            {
                if (d.name == name && d.isInstrument)
                {
                    *target = d;
                    found = true;
                    break;
                }
            }

            ok = check (found, juce::String (name) + (found ? ": " + target->fileOrIdentifier : juce::String (" no esta en la lista"))) && ok;
        }

        return ok;
    });

    action ("Agregar Dexed y Surge XT (carga asincrona)", [&panel, &s]
    {
        s.dexedId = panel.addInstance (s.dexed);
        s.surgeId = panel.addInstance (s.surge);
        return s.dexedId.isNotEmpty() && s.surgeId.isNotEmpty();
    });

    waitFor ("Esperar a que terminen de cargar", [isReady, &s] { return isReady (s.dexedId) && isReady (s.surgeId); }, kLoadTimeoutSeconds);

    action ("Estado de carga", [this, reportLoad, &s]
    {
        auto ok = reportLoad (s.dexedId, "Dexed");
        ok = reportLoad (s.surgeId, "Surge XT") && ok;
        graph.setGainDb (s.dexedId, kTestGainDb);
        graph.setGainDb (s.surgeId, kTestGainDb);
        cli::print ("  ganancia de prueba " + juce::String (kTestGainDb, 0) + " dB en ambas");
        return ok;
    });

    //--------------------------------------------------------------------------
    action ("Abrir editores de Dexed y Surge XT", [&panel, &s]
    {
        const auto a = panel.openEditor (s.dexedId);
        const auto b = panel.openEditor (s.surgeId);
        return a && b;
    });

    waitMs (1500);

    action ("Editores visibles", [this, &panel, &s]
    {
        auto ok = true;

        for (auto [label, id] : { std::pair { "Dexed", s.dexedId }, std::pair { "Surge XT", s.surgeId } })
        {
            auto* w = panel.getEditorWindow (id);
            auto* editor = w != nullptr ? w->getEditor() : nullptr;
            auto* instance = graph.findInstance (id);

            ok = check (w != nullptr && w->isVisible() && w->isOnDesktop() && editor != nullptr && editor->getWidth() > 0
                            && instance != nullptr && instance->window.open,
                        juce::String (label) + ": ventana "
                            + (editor != nullptr ? juce::String (editor->getWidth()) + "x" + juce::String (editor->getHeight())
                                                       + (w->usesGenericEditor() ? " (generico)" : " (propio)")
                                                 : juce::String ("sin editor")))
                 && ok;
        }

        return ok;
    });

    action ("Cerrar editores (Dexed por la API del panel, Surge XT por su boton de cierre)", [this, &panel, &s]
    {
        panel.closeEditor (s.dexedId);

        if (auto* w = panel.getEditorWindow (s.surgeId))
            w->closeButtonPressed(); // same path as the title bar close button; deletes the window

        auto* dexed = graph.findInstance (s.dexedId);
        auto* surge = graph.findInstance (s.surgeId);
        auto ok = check (panel.getEditorWindow (s.dexedId) == nullptr && panel.getEditorWindow (s.surgeId) == nullptr,
                         "ventanas destruidas");
        ok = check (dexed != nullptr && surge != nullptr, "las instancias siguen vivas") && ok;

        if (dexed != nullptr && surge != nullptr)
        {
            ok = check (! dexed->window.open && dexed->window.hasPosition && ! surge->window.open && surge->window.hasPosition,
                        "posicion guardada en la instancia (Dexed " + dexed->window.position.toString() + ")")
                 && ok;
            s.dexedEditorPosition = dexed->window.position;
        }

        return ok;
    });

    action ("Reabrir el editor de Dexed (misma posicion)", [this, &panel, &s]
    {
        const auto opened = panel.openEditor (s.dexedId);
        auto* w = panel.getEditorWindow (s.dexedId);
        return check (opened && w != nullptr && w->getPosition() == s.dexedEditorPosition,
                      "reabierto en " + (w != nullptr ? w->getPosition().toString() : juce::String ("-")));
    });

    action ("Abrir otra vez el mismo editor reutiliza la ventana", [this, &panel, &s]
    {
        auto* before = panel.getEditorWindow (s.dexedId);
        panel.openEditor (s.dexedId);
        return check (before != nullptr && panel.getEditorWindow (s.dexedId) == before, "misma ventana");
    });

    waitMs (500);
    action ("Cerrar el editor de Dexed", [&panel, &s] { panel.closeEditor (s.dexedId); return true; });

    //--------------------------------------------------------------------------
    playBothAndCheck ("Audio");

    action ("MIDI entregado a los plugins", [this, &s]
    {
        auto ok = true;

        for (auto [label, id] : { std::pair { "Dexed", s.dexedId }, std::pair { "Surge XT", s.surgeId } })
        {
            auto* instance = graph.findInstance (id);
            auto* midi = instance != nullptr ? instance->getMidiSource() : nullptr;
            ok = check (midi != nullptr && midi->getEventsDelivered() >= 2,
                        juce::String (label) + ": eventos MIDI encolados "
                            + juce::String (midi != nullptr ? midi->getMessagesQueued() : 0) + ", entregados "
                            + juce::String (midi != nullptr ? midi->getEventsDelivered() : 0))
                 && ok;
        }

        return ok;
    });

    action ("Mute de Dexed", [this, &panel, &keys, &s, resetPeaks]
    {
        graph.setMute (s.dexedId, true);
        resetPeaks();
        panel.selectInstance (s.dexedId);
        keys.noteOn (InstancePanel::kKeyboardChannel, 62, 0.8f);
        return true;
    });

    waitMs (150);
    action ("(descartar la rampa de 20 ms del mute)", [peakOf, &s] { peakOf (s.dexedId); return true; });
    waitMs (500);

    action ("Dexed muteado no suena", [this, &keys, &s, peakOf]
    {
        const auto peak = peakOf (s.dexedId);
        keys.noteOff (InstancePanel::kKeyboardChannel, 62, 0.0f);
        graph.setMute (s.dexedId, false);
        return check (peak == 0.0f, "Dexed muteado pico " + toDb (peak));
    });

    action ("Panico: nota colgada en Surge XT (note-on sin note-off por la cola MIDI)", [this, &s, resetPeaks]
    {
        resetPeaks();
        return graph.sendMidi (s.surgeId, juce::MidiMessage::noteOn (1, 64, 0.8f));
    });

    waitMs (500);

    action ("La nota colgada suena", [this, peakOf, &s]
    {
        const auto peak = peakOf (s.surgeId);
        return check (peak > 0.0f, "Surge XT pico " + toDb (peak));
    });

    action ("Boton Panico", [&panel] { panel.panic(); return true; });
    waitMs (1500);
    action ("(reiniciar medidores)", [resetPeaks] { resetPeaks(); return true; });
    waitMs (400);

    action ("Silencio despues del panico", [this, peakOf, &s]
    {
        const auto surgePeak = peakOf (s.surgeId);
        const auto dexedPeak = peakOf (s.dexedId);
        const auto mixerPeak = engine.getMixer() != nullptr ? engine.getMixer()->getAndResetPeak() : -1.0f;
        cli::print ("  info: Dexed " + toDb (dexedPeak) + ", mezcla " + toDb (mixerPeak));
        return check (surgePeak < 1.0e-4f, "Surge XT tras panico " + toDb (surgePeak));
    });

    //--------------------------------------------------------------------------
    action ("Cambiar el buffer de audio con instancias cargadas", [this, &s]
    {
        auto& dm = engine.getDeviceManager();
        auto* device = dm.getCurrentAudioDevice();

        if (! check (device != nullptr, "hay dispositivo abierto"))
            return false;

        s.originalType = dm.getCurrentAudioDeviceType();
        s.originalSetup = dm.getAudioDeviceSetup();
        const auto current = device->getCurrentBufferSizeSamples();

        int target = 0;

        for (auto size : device->getAvailableBufferSizes())
            if (size != current && (target == 0 || std::abs (size - 128) < std::abs (target - 128)))
                target = size;

        if (target == 0)
        {
            cli::print ("  " + s.originalType + " solo ofrece " + juce::String (current) + " muestras: se pasa a DirectSound con 128");
            dm.setCurrentAudioDeviceType ("DirectSound", true);
            target = 128;
        }

        auto setup = dm.getAudioDeviceSetup();
        setup.bufferSize = target;
        setup.sampleRate = s.originalSetup.sampleRate;
        const auto error = dm.setAudioDeviceSetup (setup, true);
        s.deviceChanged = true;

        cli::print ("  " + engine.describeDevice());
        return check (error.isEmpty() && dm.getCurrentAudioDevice() != nullptr, "dispositivo reabierto " + error)
               && check (engine.getBufferSize() != current, "buffer " + juce::String (current) + " -> " + juce::String (engine.getBufferSize()));
    });

    waitMs (800);

    const auto checkPrepared = [this, gainOf, &s] (const juce::String& label)
    {
        action (label + ": grafo re-preparado con el buffer nuevo", [this, gainOf, &s]
        {
            const auto buffer = engine.getBufferSize();
            auto ok = true;

            for (auto [name, gain] : { std::pair { "Dexed", gainOf (s.dexedId) }, std::pair { "Surge XT", gainOf (s.surgeId) },
                                       std::pair { "Mezcla", engine.getMixer() } })
            {
                ok = check (gain != nullptr && gain->getPreparedBlockSize() == buffer,
                            juce::String (name) + ": bloque preparado " + juce::String (gain != nullptr ? gain->getPreparedBlockSize() : 0)
                                + " (dispositivo " + juce::String (buffer) + ")")
                     && ok;
            }

            return ok;
        });
    };

    checkPrepared ("Buffer cambiado");
    playBothAndCheck ("Buffer cambiado");

    action ("Restaurar el dispositivo original", [this, &s]
    {
        auto& dm = engine.getDeviceManager();

        if (dm.getCurrentAudioDeviceType() != s.originalType)
            dm.setCurrentAudioDeviceType (s.originalType, true);

        const auto error = dm.setAudioDeviceSetup (s.originalSetup, true);
        s.deviceChanged = false;
        cli::print ("  " + engine.describeDevice());
        return check (error.isEmpty() && dm.getCurrentAudioDevice() != nullptr, "dispositivo restaurado " + error);
    });

    waitMs (800);
    checkPrepared ("Restaurado");
    playBothAndCheck ("Restaurado");

    //--------------------------------------------------------------------------
    action ("Agregar sforzando", [&panel, &s]
    {
        s.sforzandoId = panel.addInstance (s.sforzando);
        return s.sforzandoId.isNotEmpty();
    });

    waitFor ("Esperar a que cargue sforzando", [isReady, &s] { return isReady (s.sforzandoId); }, kLoadTimeoutSeconds);

    action ("Estado de sforzando y abrir su editor", [&panel, reportLoad, &s]
    {
        return reportLoad (s.sforzandoId, "sforzando") && panel.openEditor (s.sforzandoId);
    });

    waitMs (1500);

    action ("Editor de sforzando visible", [this, &panel, &s]
    {
        auto* w = panel.getEditorWindow (s.sforzandoId);
        auto* editor = w != nullptr ? w->getEditor() : nullptr;
        return check (w != nullptr && w->isVisible() && editor != nullptr && editor->getWidth() > 0,
                      "sforzando: ventana "
                          + (editor != nullptr ? juce::String (editor->getWidth()) + "x" + juce::String (editor->getHeight())
                                                     + (w->usesGenericEditor() ? " (generico)" : " (propio)")
                                               : juce::String ("sin editor")));
    });

    action ("Cerrar el editor y quitar sforzando", [this, &panel, &s]
    {
        panel.closeEditor (s.sforzandoId);
        panel.removeInstance (s.sforzandoId);
        return check (graph.findInstance (s.sforzandoId) == nullptr, "sforzando quitado");
    });

    waitMs (300);

    //--------------------------------------------------------------------------
    action ("Abrir el editor de Surge XT", [&panel, &s] { return panel.openEditor (s.surgeId); });
    waitMs (1000);

    action ("Quitar Surge XT con el editor abierto", [this, &panel, &s]
    {
        const auto hadWindow = panel.getEditorWindow (s.surgeId) != nullptr;
        panel.removeInstance (s.surgeId);
        auto ok = check (hadWindow, "el editor estaba abierto");
        ok = check (panel.getEditorWindow (s.surgeId) == nullptr, "ventana del editor cerrada") && ok;
        ok = check (graph.findInstance (s.surgeId) == nullptr && graph.getInstances().size() == 1, "queda solo Dexed") && ok;
        return ok;
    });

    action ("Dexed sigue sonando despues de quitar Surge XT", [&panel, &keys, &s, resetPeaks]
    {
        resetPeaks();
        panel.selectInstance (s.dexedId);
        keys.noteOn (InstancePanel::kKeyboardChannel, 60, 0.8f);
        return true;
    });

    waitMs (600);

    action ("Senal de Dexed y en la mezcla", [this, &keys, &s, peakOf]
    {
        const auto dexedPeak = peakOf (s.dexedId);
        const auto mixerPeak = engine.getMixer() != nullptr ? engine.getMixer()->getAndResetPeak() : -1.0f;
        keys.noteOff (InstancePanel::kKeyboardChannel, 60, 0.0f);
        auto ok = check (dexedPeak > 0.0f, "Dexed pico " + toDb (dexedPeak));
        return check (mixerPeak > 0.0f, "Mezcla pico " + toDb (mixerPeak)) && ok;
    });

    action ("Abrir el editor de Dexed y salir con el abierto", [&panel, &s] { return panel.openEditor (s.dexedId); });
    waitMs (800);
}

//==============================================================================
void InstanceSelfTest::buildSinglePluginSteps()
{
    auto& panel = window.getInstancePanel();
    auto& s = state();

    action ("Buscar " + singlePluginName, [this, &s]
    {
        for (auto& d : plugins.getKnownPluginList().getTypes())
        {
            if (d.name == singlePluginName)
            {
                s.dexed = d;
                return check (true, d.fileOrIdentifier);
            }
        }

        return check (false, singlePluginName + " no esta en plugins.xml");
    });

    action ("Agregar", [&panel, &s] { s.dexedId = panel.addInstance (s.dexed); return true; });

    waitFor ("Esperar la carga", [this, &s]
    {
        auto* instance = graph.findInstance (s.dexedId);
        return instance != nullptr && instance->state != Instance::State::loading;
    }, kLoadTimeoutSeconds);

    action ("Estado y editor", [this, &panel, &s]
    {
        auto* instance = graph.findInstance (s.dexedId);
        const auto ready = instance != nullptr && instance->isReady();
        graph.setGainDb (s.dexedId, kTestGainDb);
        return check (ready, ready ? juce::String ("cargado") : (instance != nullptr ? instance->errorMessage : juce::String()))
               && panel.openEditor (s.dexedId);
    });

    waitMs (1000);
    action ("Cerrar editor y tocar una nota", [this, &panel, &s]
    {
        panel.closeEditor (s.dexedId);
        return graph.sendMidi (s.dexedId, juce::MidiMessage::noteOn (1, 60, 0.8f));
    });
    waitMs (500);
    action ("Note-off y quitar", [this, &panel, &s]
    {
        graph.sendMidi (s.dexedId, juce::MidiMessage::noteOff (1, 60));
        panel.removeInstance (s.dexedId);
        return check (graph.getInstances().empty(), "sin instancias");
    });
    waitMs (300);
}

} // namespace vstplayer
