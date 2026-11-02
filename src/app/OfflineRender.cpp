#include "app/OfflineRender.h"

#include "app/AppSettings.h"
#include "app/CommandLine.h"
#include "engine/AudioEngine.h"
#include "engine/GainProcessor.h"
#include "engine/InstanceGraph.h"
#include "engine/MidiRouter.h"
#include "engine/MidiSourceProcessor.h"
#include "engine/PluginManager.h"
#include "project/ProjectController.h"

#include <algorithm>
#include <cmath>

namespace vstplayer
{

namespace
{
juce::String valueOf (const juce::StringArray& args, const juce::String& flag)
{
    const auto index = args.indexOf (flag);
    return index >= 0 && index + 1 < args.size() ? args[index + 1].unquoted() : juce::String();
}

juce::File fileArgument (const juce::String& value)
{
    return value.isEmpty() ? juce::File() : juce::File::getCurrentWorkingDirectory().getChildFile (value);
}

double toDb (float linear)
{
    return linear > 0.0f ? juce::jmax (-100.0, 20.0 * std::log10 ((double) linear)) : -100.0;
}

double rounded (double value, double decimals = 3.0)
{
    const auto scale = std::pow (10.0, decimals);
    return std::round (value * scale) / scale;
}

void progress (const juce::String& text)
{
    cli::printError ("render: " + text);
}
} // namespace

//==============================================================================
class OfflineRender::RenderThread final : public juce::Thread
{
public:
    explicit RenderThread (std::function<void()> bodyToRun)
        : Thread ("VstPlayer offline render"), body (std::move (bodyToRun))
    {
    }

    ~RenderThread() override { stopThread (60000); }

    void run() override { body(); }

private:
    std::function<void()> body;
};

//==============================================================================
OfflineRender::OfflineRender (const juce::StringArray& arguments)
    : args (arguments)
{
}

OfflineRender::~OfflineRender()
{
    stopTimer();

    if (thread != nullptr)
    {
        thread->signalThreadShouldExit();
        thread = nullptr; // joins
    }

    writer = nullptr;
    tempFile = nullptr; // deletes the temporary file if it was not renamed

    if (engine != nullptr)
        engine->getGraph().releaseResources();

    // Same order as the app's shutdown: controller, router, instances, engine.
    controller = nullptr;
    router = nullptr;
    graph = nullptr;
    engine = nullptr;
    plugins = nullptr;
    settings = nullptr; // nothing changed: saveIfNeeded writes nothing
}

//==============================================================================
bool OfflineRender::parseArguments (juce::String& error)
{
    projectFile = fileArgument (valueOf (args, "--render"));
    midiFile = fileArgument (valueOf (args, "--midi"));
    outFile = fileArgument (valueOf (args, "--out"));

    if (projectFile == juce::File() || midiFile == juce::File() || outFile == juce::File())
    {
        error = "Uso: VstPlayer.exe --render <proyecto.vstproj.json> --midi <archivo.mid> --out <archivo.wav> "
                "[--sample-rate 44100] [--block 512] [--warmup 2] [--tail-max 60] [--data-dir <carpeta>]";
        return false;
    }

    const auto number = [this, &error] (const char* flag, double minValue, double maxValue, double& target)
    {
        if (! args.contains (flag))
            return true;

        const auto text = valueOf (args, flag).trim();
        const auto value = text.getDoubleValue();

        if (text.isEmpty() || ! text.containsOnly ("0123456789.") || value < minValue || value > maxValue)
        {
            error = juce::String (flag) + juce::String::fromUTF8 (": valor inválido \"") + text + "\" (entre "
                    + juce::String (minValue) + " y " + juce::String (maxValue) + ")";
            return false;
        }

        target = value;
        return true;
    };

    double block = blockSize;

    if (! number ("--sample-rate", 8000.0, 384000.0, sampleRate) || ! number ("--block", 16.0, 8192.0, block)
        || ! number ("--warmup", 0.0, 60.0, warmupSeconds) || ! number ("--tail-max", 0.0, 600.0, tailMaxSeconds))
        return false;

    blockSize = (int) block;

    if (! projectFile.existsAsFile())
    {
        error = "No existe el proyecto: " + projectFile.getFullPathName();
        return false;
    }

    if (! midiFile.existsAsFile())
    {
        error = "No existe el archivo MIDI: " + midiFile.getFullPathName();
        return false;
    }

    if (outFile.isDirectory())
    {
        error = "--out es una carpeta: " + outFile.getFullPathName();
        return false;
    }

    return true;
}

bool OfflineRender::readMidiFile (juce::String& error)
{
    juce::FileInputStream input (midiFile);
    juce::MidiFile file;

    // No matching note-offs added: the file is played exactly as written.
    if (! input.openedOk() || ! file.readFrom (input, false, &midiFileType))
    {
        error = juce::String::fromUTF8 ("No es un archivo MIDI estándar válido: ") + midiFile.getFullPathName();
        return false;
    }

    file.convertTimestampTicksToSeconds(); // tempo map (or SMPTE time) -> seconds

    for (int t = 0; t < file.getNumTracks(); ++t)
    {
        const auto* track = file.getTrack (t);

        for (const auto* holder : *track)
        {
            const auto& message = holder->message;
            const auto status = message.getRawDataSize() > 0 ? message.getRawData()[0] : 0;

            // Channel voice messages only, as the live router (meta events are 0xff).
            if (status < 0x80 || status >= 0xf0)
                continue;

            Event event;
            event.sample = (juce::int64) std::llround (juce::jmax (0.0, message.getTimeStamp()) * sampleRate);
            event.message = message;
            events.push_back (event);
        }
    }

    // Track order, then file order within a track, for events on the same sample.
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.sample < b.sample; });
    lastEventSample = events.empty() ? 0 : events.back().sample;
    return true;
}

//==============================================================================
void OfflineRender::start()
{
    startMs = juce::Time::getMillisecondCounterHiRes();
    juce::String error;

    if (! parseArguments (error) || ! readMidiFile (error))
    {
        fail (kExitBadArguments, error);
        return;
    }

    const auto pluginList = PluginManager::getPluginListFile();

    if (! pluginList.existsAsFile())
    {
        fail (kExitRenderError, "No hay lista de plugins (" + pluginList.getFullPathName()
                                    + juce::String::fromUTF8 ("): escanear antes con VstPlayer.exe --scan o desde la app."));
        return;
    }

    progress ("proyecto " + projectFile.getFullPathName() + ", MIDI " + midiFile.getFullPathName() + " (tipo "
              + juce::String (midiFileType) + ", " + juce::String ((int) events.size()) + " eventos, "
              + juce::String ((double) lastEventSample / sampleRate, 3) + " s)");

    // Read-only settings and plugin list, no device, no ports, no recent list or autosave.
    settings = std::make_unique<AppSettings>();
    plugins = std::make_unique<PluginManager> (*settings, false);
    engine = std::make_unique<AudioEngine> (*settings, false);
    engine->setOfflineFormat (sampleRate, blockSize); // plugins are created at the render format
    graph = std::make_unique<InstanceGraph> (*engine, *plugins);
    router = std::make_unique<MidiRouter> (*graph, false);
    controller = std::make_unique<ProjectController> (*engine, *plugins, *graph, *router, juce::File());
    controller->offlineRender = true;

    const auto result = controller->open (projectFile);

    if (result.failed())
    {
        fail (kExitBadArguments, "No se pudo leer el proyecto: " + result.getErrorMessage());
        return;
    }

    loading = true;
    loadStartMs = juce::Time::getMillisecondCounterHiRes();
    startTimer (50);
}

void OfflineRender::timerCallback()
{
    if (! loading)
        return;

    if (controller->isLoading() || graph->getNumLoading() > 0 || graph->isCreationPending())
    {
        if (juce::Time::getMillisecondCounterHiRes() - loadStartMs > kLoadTimeoutSeconds * 1000.0)
        {
            loading = false;
            stopTimer();
            fail (kExitRenderError, "Timeout: los plugins no terminaron de cargar en " + juce::String ((int) kLoadTimeoutSeconds) + " s.");
        }

        return;
    }

    loading = false;
    stopTimer();
    instancesLoaded();
}

void OfflineRender::instancesLoaded()
{
    JUCE_ASSERT_MESSAGE_THREAD

    juce::StringArray problems;

    for (auto& instance : graph->getInstances())
        if (! instance->isReady())
            problems.add ("\"" + instance->name + "\": "
                          + (instance->errorMessage.isNotEmpty() ? instance->errorMessage : juce::String ("no se pudo cargar")));

    // Load problems that are not about a missing / failed instance (bad state, unknown params).
    for (auto& warning : controller->getLastWarnings())
        warnings.addIfNotAlreadyThere (warning);

    if (! problems.isEmpty())
    {
        fail (kExitRenderError, "Instancias que no cargaron: " + problems.joinIntoString ("; "));
        return;
    }

    if (graph->getInstances().empty())
        warnings.add ("El proyecto no tiene instancias: el WAV queda en silencio.");

    progress (juce::String ((int) graph->getInstances().size()) + " instancias listas en "
              + juce::String ((juce::Time::getMillisecondCounterHiRes() - loadStartMs) / 1000.0, 1) + " s");

    buildRoutingTable();

    // Output file first: a target that cannot be written fails before rendering.
    outFile.getParentDirectory().createDirectory();
    tempFile = std::make_unique<juce::TemporaryFile> (outFile);
    std::unique_ptr<juce::OutputStream> stream = tempFile->getFile().createOutputStream();

    if (stream != nullptr)
    {
        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions()
                                 .withSampleRate (sampleRate)
                                 .withNumChannels (2)
                                 .withBitsPerSample (32)
                                 .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        writer = wav.createWriterFor (stream, options);
    }

    if (writer == nullptr)
    {
        fail (kExitRenderError, "No se pudo escribir " + tempFile->getFile().getFullPathName());
        return;
    }

    // Plugins learn about the offline mode in prepareToPlay (VST3 processMode = kOffline), which
    // JUCE requires on the message thread.
    auto& audioGraph = engine->getGraph();
    audioGraph.setNonRealtime (true);
    audioGraph.prepareToPlay (sampleRate, blockSize);
    latencySamples = juce::jmax (0, audioGraph.getLatencySamples());
    latencyToSkip = latencySamples;

    if (latencySamples > 0)
        warnings.add ("Latencia del grafo: " + juce::String (latencySamples) + " muestras (compensada).");

    const auto tailCapacity = (int) std::ceil (tailMaxSeconds * sampleRate) + 2 * blockSize;
    tail.setSize (2, tailCapacity);
    tail.clear();

    for (auto& instance : graph->getInstances())
        if (auto* gain = instance->getGainProcessor())
            gain->getAndResetPeak();

    juce::WeakReference<OfflineRender> weakThis (this);
    thread = std::make_unique<RenderThread> ([this, weakThis]
    {
        render();
        renderDone = true;

        juce::MessageManager::callAsync ([weakThis]
        {
            if (auto* self = weakThis.get())
                self->renderFinished();
        });
    });

    renderStartMs = juce::Time::getMillisecondCounterHiRes();
    thread->startThread (juce::Thread::Priority::high);
}

void OfflineRender::buildRoutingTable()
{
    // As carrilion's vstplayer-port.js: every route counts, whatever its input device; one
    // delivery per (instance, output channel) for each source channel.
    for (auto& route : router->getRoutes())
    {
        auto* instance = graph->findInstance (route.instanceId);
        auto* midi = instance != nullptr && instance->isReady() ? instance->getMidiSource() : nullptr;

        if (midi == nullptr)
            continue;

        for (int channel = 1; channel <= 16; ++channel)
        {
            if (! route.hasChannel (channel))
                continue;

            const auto outChannel = route.isKeep() ? channel : route.remapChannel;
            auto& targets = routing[channel];
            const auto duplicate = std::any_of (targets.begin(), targets.end(), [&] (const Target& t)
                                                { return t.midi == midi && t.outChannel == outChannel; });

            if (! duplicate)
                targets.push_back ({ midi, outChannel });
        }
    }

    if (router->getRoutes().empty() && ! graph->getInstances().empty())
        warnings.add ("El proyecto no tiene rutas MIDI: ninguna instancia recibe el archivo MIDI.");
}

//==============================================================================
void OfflineRender::route (const Event& event, int samplePosition)
{
    const auto channel = event.message.getChannel();
    const auto& targets = routing[juce::jlimit (0, 16, channel)];

    if (channel < 1 || targets.empty())
    {
        ++messagesUnrouted;
        return;
    }

    ++messagesRouted;

    for (auto& target : targets)
    {
        auto out = event.message;

        if (target.outChannel != channel)
            out.setChannel (target.outChannel);

        // Held notes as the live router (MidiRouter::deliverLocked): a note-on for a note that
        // is already sounding on that instance and channel ends it first.
        auto& notes = held[target.midi][(size_t) target.outChannel - 1];

        if (out.isNoteOn())
        {
            const auto note = (size_t) out.getNoteNumber();

            if (notes.test (note))
                target.midi->addOfflineMessage (juce::MidiMessage::noteOff (target.outChannel, (int) note), samplePosition);

            notes.set (note);
        }
        else if (out.isNoteOff())
        {
            notes.reset ((size_t) out.getNoteNumber());
        }
        else if (out.isAllNotesOff() || out.isAllSoundOff())
        {
            notes.reset();
        }

        target.midi->addOfflineMessage (out, samplePosition);
        ++deliveries;
    }
}

void OfflineRender::consume (const juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto start = 0;

    if (latencyToSkip > 0)
    {
        start = (int) juce::jmin ((juce::int64) numSamples, latencyToSkip);
        latencyToSkip -= start;
    }

    // Before the last event: straight to the file.
    const auto beforeTail = (int) juce::jlimit ((juce::int64) 0, (juce::int64) (numSamples - start), lastEventSample - outCount);

    if (beforeTail > 0)
    {
        if (! writer->writeFromAudioSampleBuffer (buffer, start, beforeTail))
            writeFailed = true;

        for (int ch = 0; ch < 2; ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, start, beforeTail));

        framesWritten += beforeTail;
        outCount += beforeTail;
        start += beforeTail;
    }

    // From the last event on: kept until the end is known.
    const auto rest = juce::jmin (numSamples - start, tail.getNumSamples() - (int) tailSamples);

    for (int i = 0; i < rest; ++i)
    {
        const auto l = buffer.getSample (0, start + i);
        const auto r = buffer.getSample (1, start + i);
        tail.setSample (0, (int) tailSamples + i, l);
        tail.setSample (1, (int) tailSamples + i, r);

        if (std::abs (l) > kSilenceThreshold || std::abs (r) > kSilenceThreshold)
            lastAudible = outCount + i;
    }

    tailSamples += rest;
    outCount += rest;
}

void OfflineRender::render()
{
    auto& audioGraph = engine->getGraph();
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    // Warm-up: silence at real-time pace (background sample loading needs wall-clock time).
    const auto warmupBlocks = (juce::int64) std::ceil (warmupSeconds * sampleRate / blockSize);
    const auto warmupStart = juce::Time::getMillisecondCounterHiRes();

    if (warmupBlocks > 0)
        progress ("calentamiento " + juce::String (warmupSeconds, 1) + " s");

    for (juce::int64 b = 0; b < warmupBlocks && ! juce::Thread::currentThreadShouldExit(); ++b)
    {
        buffer.clear();
        midi.clear();
        audioGraph.processBlock (buffer, midi);

        const auto due = warmupStart + 1000.0 * (double) ((b + 1) * blockSize) / sampleRate;

        while (juce::Time::getMillisecondCounterHiRes() < due && ! juce::Thread::currentThreadShouldExit())
            juce::Thread::sleep (juce::jlimit (1, 20, (int) (due - juce::Time::getMillisecondCounterHiRes())));
    }

    for (auto& instance : graph->getInstances())
        if (auto* gain = instance->getGainProcessor())
            gain->getAndResetPeak();

    const auto silenceHold = (juce::int64) std::llround (kSilenceHoldSeconds * sampleRate);
    const auto tailMax = (juce::int64) std::llround (tailMaxSeconds * sampleRate);
    const auto progressStep = juce::jmax ((juce::int64) 1, lastEventSample / 10);
    juce::int64 nextProgress = progressStep;
    juce::int64 renderPos = 0; // MIDI time, in samples
    size_t next = 0;

    renderStartMs = juce::Time::getMillisecondCounterHiRes();

    while (! juce::Thread::currentThreadShouldExit())
    {
        while (next < events.size() && events[next].sample < renderPos + blockSize)
        {
            route (events[next], (int) (events[next].sample - renderPos));
            ++next;
        }

        buffer.clear();
        midi.clear();
        audioGraph.processBlock (buffer, midi);
        consume (buffer, blockSize);
        renderPos += blockSize;

        if (lastEventSample > 0 && renderPos >= nextProgress && renderPos < lastEventSample)
        {
            progress (juce::String (juce::roundToInt (100.0 * (double) renderPos / (double) lastEventSample)) + " %");
            nextProgress += progressStep;
        }

        if (writeFailed)
            break;

        if (next >= events.size() && outCount >= lastEventSample)
        {
            const auto quietSince = juce::jmax (lastEventSample, lastAudible + 1);

            if (outCount - lastEventSample >= tailMax)
            {
                tailCut = lastAudible + 1 > outCount - silenceHold;
                break;
            }

            if (outCount - quietSince >= silenceHold)
                break;
        }
    }

    renderEndMs = juce::Time::getMillisecondCounterHiRes();

    if (juce::Thread::currentThreadShouldExit())
    {
        renderError = "Render interrumpido.";
        return;
    }

    // Final length (carrilion's audible_end): 0.5 s after the last sample above -70 dBFS, never
    // before the last event, never past what was rendered.
    const auto pad = (juce::int64) std::llround (kSilencePadSeconds * sampleRate);
    auto end = lastAudible + 1 > lastEventSample ? lastAudible + 1 + pad : lastEventSample;
    end = juce::jmin (end, lastEventSample + tailSamples);
    const auto fromTail = (int) juce::jmax ((juce::int64) 0, end - lastEventSample);

    if (fromTail > 0)
    {
        if (! writer->writeFromAudioSampleBuffer (tail, 0, fromTail))
            writeFailed = true;

        for (int ch = 0; ch < 2; ++ch)
            peak = juce::jmax (peak, tail.getMagnitude (ch, 0, fromTail));

        framesWritten += fromTail;
    }

    if (! writer->flush())
        writeFailed = true;

    writer = nullptr; // completes the WAV header

    if (writeFailed)
        renderError = "Error al escribir " + tempFile->getFile().getFullPathName();
}

void OfflineRender::renderFinished()
{
    JUCE_ASSERT_MESSAGE_THREAD

    thread = nullptr; // already finished: joins at once
    engine->getGraph().releaseResources();

    if (renderError.isNotEmpty())
    {
        fail (kExitRenderError, renderError);
        return;
    }

    if (! tempFile->overwriteTargetFileWithTemporary())
    {
        fail (kExitRenderError, "No se pudo reemplazar " + outFile.getFullPathName());
        return;
    }

    int hanging = 0;

    for (auto& [midi, channels] : held)
        for (auto& notes : channels)
            hanging += (int) notes.count();


    if (hanging > 0)
        warnings.add (juce::String (hanging) + " notas quedaron sin note-off al final del archivo MIDI.");

    if (messagesUnrouted > 0)
        warnings.add (juce::String (messagesUnrouted) + juce::String::fromUTF8 (" mensajes MIDI en canales sin ruta (ignorados)."));

    if (tailCut)
        warnings.add ("La cola se cortó en --tail-max (" + juce::String (tailMaxSeconds, 1) + " s) con sonido todavía.");

    const auto seconds = (double) framesWritten / sampleRate;
    const auto lastEventSeconds = (double) lastEventSample / sampleRate;

    juce::Array<juce::var> instanceList;

    for (auto& instance : graph->getInstances())
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("id", instance->id);
        obj->setProperty ("name", instance->name);
        obj->setProperty ("plugin", instance->description.name);

        if (auto* midi = instance->getMidiSource())
        {
            obj->setProperty ("midiEvents", midi->getEventsDelivered());
            obj->setProperty ("mixControllers", midi->getMixControllersTaken());
            juce::Array<juce::var> noteOns;

            for (int channel = 1; channel <= 16; ++channel)
                noteOns.add (midi->getNoteOnsDelivered (channel));

            obj->setProperty ("noteOns", noteOns);
        }

        if (auto* gain = instance->getGainProcessor())
            obj->setProperty ("peakDb", rounded (toDb (gain->getAndResetPeak()), 2));

        instanceList.add (juce::var (obj));
    }

    auto* summary = new juce::DynamicObject();
    summary->setProperty ("ok", true);
    summary->setProperty ("out", outFile.getFullPathName());
    summary->setProperty ("frames", framesWritten);
    summary->setProperty ("sampleRate", sampleRate);
    summary->setProperty ("blockSize", blockSize);
    summary->setProperty ("seconds", rounded (seconds, 6));
    summary->setProperty ("lastEventSeconds", rounded (lastEventSeconds, 6));
    summary->setProperty ("tailSeconds", rounded (seconds - lastEventSeconds, 6));
    summary->setProperty ("peakDb", rounded (toDb (peak), 2));
    summary->setProperty ("midiFileType", midiFileType);
    summary->setProperty ("midiEvents", (int) events.size());
    summary->setProperty ("midiRouted", messagesRouted);
    summary->setProperty ("midiUnrouted", messagesUnrouted);
    summary->setProperty ("deliveries", deliveries);
    summary->setProperty ("warmupSeconds", warmupSeconds);
    summary->setProperty ("latencySamples", latencySamples);
    summary->setProperty ("renderMs", rounded (renderEndMs - renderStartMs, 1));
    summary->setProperty ("totalMs", rounded (juce::Time::getMillisecondCounterHiRes() - startMs, 1));
    summary->setProperty ("instances", instanceList);

    juce::Array<juce::var> warningList;

    for (auto& w : warnings)
        warningList.add (w);

    summary->setProperty ("warnings", warningList);

    progress ("listo: " + outFile.getFullPathName() + " (" + juce::String (seconds, 2) + " s de audio en "
              + juce::String ((renderEndMs - renderStartMs) / 1000.0, 2) + " s)");
    cli::print (juce::JSON::toString (juce::var (summary), true, 6));
    finish (kExitOk);
}

//==============================================================================
void OfflineRender::fail (int exitCode, const juce::String& error)
{
    progress ("ERROR: " + error);

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("ok", false);
    obj->setProperty ("error", error);
    juce::Array<juce::var> warningList;

    for (auto& w : warnings)
        warningList.add (w);

    obj->setProperty ("warnings", warningList);
    cli::print (juce::JSON::toString (juce::var (obj), true, 6));

    writer = nullptr;
    tempFile = nullptr; // removes the partial file
    finish (exitCode);
}

void OfflineRender::finish (int exitCode)
{
    if (finished)
        return;

    finished = true;

    if (onFinished)
        onFinished (exitCode);
}

} // namespace vstplayer
