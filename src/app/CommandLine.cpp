#include "app/CommandLine.h"

#include "app/AppSettings.h"
#include "engine/AudioEngine.h"
#include "engine/PluginManager.h"
#include "engine/ToneGenerator.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdlib>
#include <cstring>

namespace vstplayer::cli
{

namespace
{
HANDLE getOutputHandle()
{
    static HANDLE handle = []
    {
        auto h = GetStdHandle (STD_OUTPUT_HANDLE);

        if (h == nullptr || h == INVALID_HANDLE_VALUE)
        {
            // Not redirected: try to write to the console we were launched from, if any.
            if (AttachConsole (ATTACH_PARENT_PROCESS))
                h = CreateFileW (L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        }

        return h;
    }();

    return handle;
}

LONG WINAPI terminateOnCrash (EXCEPTION_POINTERS*)
{
    // A plugin crashed inside the scanner child: die quietly, the parent blacklists the file.
    TerminateProcess (GetCurrentProcess(), 0xC0DE0001);
    return EXCEPTION_EXECUTE_HANDLER;
}

juce::String describe (const juce::PluginDescription& d)
{
    return "  " + d.name + " | " + d.manufacturerName + " | v" + d.version
           + " | " + (d.isInstrument ? "instrumento" : "efecto")
           + " | " + d.fileOrIdentifier;
}
} // namespace

Mode detectMode (const juce::StringArray& args)
{
    if (args.contains ("--scan-one"))
        return Mode::scanChild;

    if (args.contains ("--scan"))
        return Mode::scan;

    if (args.contains ("--audio-selftest"))
        return Mode::audioSelfTest;

    if (args.contains ("--audio-devices"))
        return Mode::audioDevices;

    return Mode::gui;
}

void print (const juce::String& line)
{
    const auto handle = getOutputHandle();

    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
        return;

    const auto text = (line + "\r\n").toStdString(); // UTF-8
    DWORD written = 0;
    WriteFile (handle, text.data(), (DWORD) text.size(), &written, nullptr);
}

namespace
{
LONG CALLBACK captureDebugString (EXCEPTION_POINTERS* info)
{
    // OutputDebugStringW ends up raising the ANSI variant too when no debugger handles it, so
    // only DBG_PRINTEXCEPTION_C is copied. Plain Win32 here: this also runs during static
    // destruction (leak detector).
    const auto* record = info->ExceptionRecord;

    if (record->ExceptionCode == DBG_PRINTEXCEPTION_C && record->NumberParameters >= 2)
    {
        const auto length = (size_t) record->ExceptionInformation[0];
        const auto* text = reinterpret_cast<const char*> (record->ExceptionInformation[1]);
        const auto handle = getOutputHandle();

        if (text != nullptr && length > 0 && handle != nullptr && handle != INVALID_HANDLE_VALUE)
        {
            auto n = strnlen (text, length);

            while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r'))
                --n;

            DWORD written = 0;
            WriteFile (handle, "[debug] ", 8, &written, nullptr);
            WriteFile (handle, text, (DWORD) n, &written, nullptr);
            WriteFile (handle, "\r\n", 2, &written, nullptr);
        }
    }

    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

void installDebugOutputCapture()
{
    static bool installed = false;

    if (! installed)
    {
        getOutputHandle(); // resolve now, not inside the handler
        AddVectoredExceptionHandler (1, captureDebugString);
        installed = true;
    }
}

int runScanChild (const juce::StringArray& args)
{
    // No error dialogs (WER, missing DLL boxes) from a crashing plugin: just exit.
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter (terminateOnCrash);

    const auto index = args.indexOf ("--scan-one");

    if (index < 0 || index + 2 >= args.size())
        return PluginManager::kChildExitBadArgs;

    const auto pluginFile = args[index + 1].unquoted();
    const auto outputFile = juce::File (args[index + 2].unquoted());

    return PluginManager::runScanChild (pluginFile, outputFile);
}

int runScan (const juce::StringArray& args, AppSettings& settings)
{
    const auto force = args.contains ("--force");
    PluginManager manager (settings);

    const auto alreadyKnown = manager.getKnownPluginList().getNumTypes();
    print ("VstPlayer --scan" + juce::String (force ? " --force" : ""));
    print ("plugins.xml: " + PluginManager::getPluginListFile().getFullPathName()
           + (PluginManager::getPluginListFile().existsAsFile() ? " (existe)" : " (no existe)"));
    print ("Plugins conocidos antes del escaneo: " + juce::String (alreadyKnown));

    const auto startMs = juce::Time::getMillisecondCounterHiRes();
    const auto report = manager.scanBlocking (force, [] (const juce::String& s) { print (s); });
    const auto seconds = (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0;

    print ("Archivos candidatos: " + juce::String (report.candidateFiles)
           + ", procesos hijos lanzados: " + juce::String (report.childProcesses)
           + ", tiempo: " + juce::String (seconds, 1) + " s");

    auto& list = manager.getKnownPluginList();
    print ("Plugins en la lista: " + juce::String (list.getNumTypes()));

    for (auto& d : list.getTypes())
        print (describe (d));

    print ("Desactivados (blacklist): " + juce::String (list.getBlacklistedFiles().size()));

    for (auto& f : list.getBlacklistedFiles())
        print ("  " + f + (report.newlyBlacklisted.contains (f) ? "  [nuevo]" : ""));

    for (auto& f : report.failedFiles)
        print ("Fallo sin blacklist: " + f);

    const auto saved = manager.save();
    print (juce::String ("plugins.xml ") + (saved ? "escrito" : "NO se pudo escribir"));
    settings.flush();

    return saved ? 0 : 1;
}

int runAudioSelfTest (const juce::StringArray& args, AppSettings& settings)
{
    AudioEngine engine (settings, false);
    auto error = engine.initialise();
    auto& dm = engine.getDeviceManager();

    print ("VstPlayer --audio-selftest");

    // Optional: --type "<device type name>" switches type first (at 256 samples if possible).
    if (const auto typeIndex = args.indexOf ("--type"); typeIndex >= 0 && typeIndex + 1 < args.size())
    {
        dm.setCurrentAudioDeviceType (args[typeIndex + 1].unquoted(), true);
        auto setup = dm.getAudioDeviceSetup();
        setup.bufferSize = AudioEngine::kDefaultBufferSize;
        setup.sampleRate = AudioEngine::kDefaultSampleRate;
        error = dm.setAudioDeviceSetup (setup, true);
    }

    print (engine.describeDevice());

    if (error.isNotEmpty() || dm.getCurrentAudioDevice() == nullptr)
    {
        print ("ERROR al abrir el audio: " + error);
        return 1;
    }

    auto* tone = engine.getToneGenerator();

    if (tone == nullptr)
    {
        print ("ERROR: no hay nodo de tono");
        return 1;
    }

    // Very low level: the test must not blast the speakers.
    tone->setLevel (0.01f);
    engine.setTestToneEnabled (true);

    bool ok = true;

    const auto measure = [&] (const juce::String& label, int expectedBuffer)
    {
        juce::Thread::sleep (500); // let the device settle after (re)starting
        const auto blocksBefore = tone->getBlocksProcessed();
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        juce::Thread::sleep (2000);
        const auto blocks = tone->getBlocksProcessed() - blocksBefore;
        const auto elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;

        const auto bufferSize = engine.getBufferSize();
        const auto latency = engine.getOutputLatencySamples();
        const auto sampleRate = engine.getSampleRate();
        // Samples actually rendered per second: close to the sample rate if nothing drops out.
        const auto effectiveRate = (double) blocks * tone->getLastBlockSize() / elapsedSeconds;

        print (label + ": device sr=" + juce::String (sampleRate, 0) + " buffer=" + juce::String (bufferSize)
               + " latencia salida=" + juce::String (latency) + " muestras ("
               + juce::String (sampleRate > 0 ? 1000.0 * latency / sampleRate : 0.0, 2) + " ms)"
               + " | grafo preparado: sr=" + juce::String (tone->getPreparedSampleRate(), 0)
               + " bloque=" + juce::String (tone->getPreparedBlockSize())
               + " (prepareToPlay x" + juce::String (tone->getPrepareCount()) + ")"
               + " | bloques=" + juce::String (blocks) + " en " + juce::String (elapsedSeconds, 2) + " s"
               + " (" + juce::String (juce::roundToInt (effectiveRate)) + " muestras/s)"
               + " ultimo bloque=" + juce::String (tone->getLastBlockSize())
               + " pico=" + juce::String (tone->getLastPeak(), 4));

        // Informational only: some drivers legitimately call back in bursts (DirectSound),
        // but a sustained rate far below the sample rate means audible dropouts.
        if (std::abs (effectiveRate - sampleRate) > 0.05 * sampleRate)
            print (label + ": AVISO ritmo de render " + juce::String (juce::roundToInt (effectiveRate))
                   + " muestras/s, distinto de " + juce::String (juce::roundToInt (sampleRate)));

        const auto stepOk = blocks > 0
                            && tone->getLastPeak() > 0.0f
                            && tone->getPreparedBlockSize() == bufferSize
                            && juce::approximatelyEqual (tone->getPreparedSampleRate(), sampleRate)
                            && (expectedBuffer <= 0 || bufferSize == expectedBuffer);

        print (label + (stepOk ? ": OK" : ": FALLO"));
        ok = ok && stepOk;
    };

    measure ("Inicial", 0);

    // Buffer change: 128 if the device offers it, otherwise the closest other size.
    const auto current = engine.getBufferSize();
    int target = 0;

    for (auto size : dm.getCurrentAudioDevice()->getAvailableBufferSizes())
        if (size != current && (target == 0 || std::abs (size - 128) < std::abs (target - 128)))
            target = size;

    if (target == 0)
    {
        print ("Cambio de buffer: OMITIDO (el dispositivo solo ofrece " + juce::String (current)
               + " muestras con el tipo " + engine.getDeviceTypeName() + ")");
    }
    else
    {
        const auto preparesBefore = tone->getPrepareCount();
        auto setup = dm.getAudioDeviceSetup();
        setup.bufferSize = target;
        const auto setupError = dm.setAudioDeviceSetup (setup, true);

        if (setupError.isNotEmpty())
        {
            print ("ERROR al cambiar buffer a " + juce::String (target) + ": " + setupError);
            ok = false;
        }

        measure ("Buffer " + juce::String (target), target);

        if (tone->getPrepareCount() <= preparesBefore)
        {
            print ("FALLO: el grafo no se re-preparo tras cambiar el buffer");
            ok = false;
        }
    }

    engine.setTestToneEnabled (false);
    print (ok ? "RESULTADO: OK" : "RESULTADO: FALLO");
    return ok ? 0 : 1;
}

int runAudioDevices()
{
    juce::AudioDeviceManager dm;
    print ("VstPlayer --audio-devices");

    for (auto* type : dm.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        const auto outputs = type->getDeviceNames (false);
        print ("[" + type->getTypeName() + "] salidas: " + juce::String (outputs.size()));

        const auto defaultIndex = type->getDefaultDeviceIndex (false);

        if (! juce::isPositiveAndBelow (defaultIndex, outputs.size()))
            continue;

        const auto name = outputs[defaultIndex];
        std::unique_ptr<juce::AudioIODevice> device (type->createDevice (name, {}));

        if (device == nullptr)
            continue;

        juce::StringArray rates, sizes;

        for (auto r : device->getAvailableSampleRates())
            rates.add (juce::String (r, 0));

        for (auto b : device->getAvailableBufferSizes())
            sizes.add (juce::String (b));

        print ("  por defecto: " + name);
        print ("  sample rates: " + rates.joinIntoString (" "));
        print ("  buffers: " + sizes.joinIntoString (" ") + " (default " + juce::String (device->getDefaultBufferSize()) + ")");
    }

    return 0;
}

} // namespace vstplayer::cli
