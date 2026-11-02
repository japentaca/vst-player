#include "app/CommandLine.h"

#include "app/AppSettings.h"
#include "engine/AudioEngine.h"
#include "engine/PluginManager.h"
#include "engine/GainProcessor.h"

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

HANDLE getErrorHandle()
{
    static HANDLE handle = []
    {
        auto h = GetStdHandle (STD_ERROR_HANDLE);

        if (h == nullptr || h == INVALID_HANDLE_VALUE)
        {
            getOutputHandle(); // attaches to the parent console if stdout is not redirected either
            AttachConsole (ATTACH_PARENT_PROCESS); // fails harmlessly when already attached / no console
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

    if (args.contains ("--plugin-folders"))
        return Mode::pluginFolders;

    if (args.contains ("--add-plugin-folder"))
        return Mode::addPluginFolder;

    if (args.contains ("--remove-plugin-folder"))
        return Mode::removePluginFolder;

    if (args.contains ("--audio-selftest"))
        return Mode::audioSelfTest;

    if (args.contains ("--audio-devices"))
        return Mode::audioDevices;

    if (args.contains ("--midi-send"))
        return Mode::midiSend;

    if (args.contains ("--midi-ports"))
        return Mode::midiPorts;

    if (args.contains ("--midi-monitor"))
        return Mode::midiMonitor;

    if (args.contains ("--render"))
        return Mode::render;

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

void printError (const juce::String& line)
{
    const auto handle = getErrorHandle();

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

int runPluginFolders (const juce::StringArray& args, AppSettings& settings)
{
    PluginManager manager (settings);

    const auto printFolders = [&manager]
    {
        const auto defaults = manager.getDefaultFolders();
        const auto extras = manager.getExtraFolders();
        print ("Carpetas de busqueda VST3:");

        for (int i = 0; i < defaults.getNumPaths(); ++i)
            print ("  [predeterminada] " + defaults[i].getFullPathName() + (defaults[i].isDirectory() ? "" : "  (no existe)"));

        for (int i = 0; i < extras.getNumPaths(); ++i)
            print ("  [extra]          " + extras[i].getFullPathName() + (extras[i].isDirectory() ? "" : "  (no existe)"));
    };

    const auto folderArg = [&args] (const juce::String& flag)
    {
        const auto index = args.indexOf (flag);
        const auto value = index >= 0 ? args[index + 1].unquoted() : juce::String();
        return value.isEmpty() ? juce::File() : juce::File::getCurrentWorkingDirectory().getChildFile (value);
    };

    int exitCode = 0;

    if (args.contains ("--add-plugin-folder"))
    {
        const auto folder = folderArg ("--add-plugin-folder");

        if (folder == juce::File())
        {
            print ("Falta la carpeta: --add-plugin-folder <carpeta>");
            return 1;
        }

        const auto result = manager.addExtraFolder (folder);
        print (PluginManager::describe (result, folder));

        if (result == PluginManager::AddFolderResult::added || result == PluginManager::AddFolderResult::alreadyPresent)
        {
            const auto before = manager.getKnownPluginList().getNumTypes();
            const auto report = manager.scanBlocking (false, [] (const juce::String& s) { print (s); });
            print ("Escaneo: " + juce::String (report.childProcesses) + " archivos nuevos probados, "
                   + juce::String (manager.getKnownPluginList().getNumTypes() - before) + " plugins nuevos, "
                   + juce::String (report.newlyBlacklisted.size()) + " desactivados");

            for (auto& f : report.newlyBlacklisted)
                print ("  desactivado: " + f);
        }
        else
        {
            exitCode = 1;
        }
    }
    else if (args.contains ("--remove-plugin-folder"))
    {
        const auto folder = folderArg ("--remove-plugin-folder");
        const auto removed = folder == juce::File() ? -1 : manager.removeExtraFolder (folder);

        if (removed < 0)
        {
            print ("No es una carpeta extra: " + folder.getFullPathName());
            exitCode = 1;
        }
        else
        {
            print ("Carpeta quitada: " + folder.getFullPathName() + " (" + juce::String (removed)
                   + " entradas quitadas de plugins.xml)");
        }
    }

    printFolders();
    print ("Plugins en la lista: " + juce::String (manager.getKnownPluginList().getNumTypes())
           + ", desactivados: " + juce::String (manager.getKnownPluginList().getBlacklistedFiles().size()));

    if (! manager.save())
    {
        print ("plugins.xml NO se pudo escribir");
        exitCode = 1;
    }

    settings.flush();
    return exitCode;
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

    // Since stage 6 there is no test tone: the graph runs silent and the output mixer node
    // (always present) is the probe: blocks processed, prepareToPlay arguments, block size.
    auto* mixer = engine.getMixer();

    if (mixer == nullptr)
    {
        print ("ERROR: no hay nodo de mezcla");
        return 1;
    }

    bool ok = true;

    const auto measure = [&] (const juce::String& label, int expectedBuffer)
    {
        juce::Thread::sleep (500); // let the device settle after (re)starting
        const auto blocksBefore = mixer->getBlocksProcessed();
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        juce::Thread::sleep (2000);
        const auto blocks = mixer->getBlocksProcessed() - blocksBefore;
        const auto elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;

        const auto bufferSize = engine.getBufferSize();
        const auto latency = engine.getOutputLatencySamples();
        const auto sampleRate = engine.getSampleRate();
        // Samples actually rendered per second: close to the sample rate if nothing drops out.
        const auto effectiveRate = (double) blocks * mixer->getLastBlockSize() / elapsedSeconds;

        print (label + ": device sr=" + juce::String (sampleRate, 0) + " buffer=" + juce::String (bufferSize)
               + " latencia salida=" + juce::String (latency) + " muestras ("
               + juce::String (sampleRate > 0 ? 1000.0 * latency / sampleRate : 0.0, 2) + " ms)"
               + " | grafo preparado: sr=" + juce::String (mixer->getPreparedSampleRate(), 0)
               + " bloque=" + juce::String (mixer->getPreparedBlockSize())
               + " (prepareToPlay x" + juce::String (mixer->getPrepareCount()) + ")"
               + " | bloques=" + juce::String (blocks) + " en " + juce::String (elapsedSeconds, 2) + " s"
               + " (" + juce::String (juce::roundToInt (effectiveRate)) + " muestras/s)"
               + " ultimo bloque=" + juce::String (mixer->getLastBlockSize())
               + " CPU=" + juce::String (100.0 * engine.getCpuUsage(), 1) + " %");

        // Informational only: some drivers legitimately call back in bursts (DirectSound),
        // but a sustained rate far below the sample rate means audible dropouts.
        if (std::abs (effectiveRate - sampleRate) > 0.05 * sampleRate)
            print (label + ": AVISO ritmo de render " + juce::String (juce::roundToInt (effectiveRate))
                   + " muestras/s, distinto de " + juce::String (juce::roundToInt (sampleRate)));

        const auto stepOk = blocks > 0
                            && mixer->getPreparedBlockSize() == bufferSize
                            && juce::approximatelyEqual (mixer->getPreparedSampleRate(), sampleRate)
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
        const auto preparesBefore = mixer->getPrepareCount();
        auto setup = dm.getAudioDeviceSetup();
        setup.bufferSize = target;
        const auto setupError = dm.setAudioDeviceSetup (setup, true);

        if (setupError.isNotEmpty())
        {
            print ("ERROR al cambiar buffer a " + juce::String (target) + ": " + setupError);
            ok = false;
        }

        measure ("Buffer " + juce::String (target), target);

        if (mixer->getPrepareCount() <= preparesBefore)
        {
            print ("FALLO: el grafo no se re-preparo tras cambiar el buffer");
            ok = false;
        }
    }

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


//==============================================================================
int runMidiPorts()
{
    print ("VstPlayer --midi-ports");
    print ("Entradas:");

    for (auto& device : juce::MidiInput::getAvailableDevices())
        print ("  " + device.name + "  [" + device.identifier + "]");

    print ("Salidas:");

    for (auto& device : juce::MidiOutput::getAvailableDevices())
        print ("  " + device.name + "  [" + device.identifier + "]");

    return 0;
}

int runMidiSend (const juce::StringArray& args)
{
    const auto index = args.indexOf ("--midi-send");

    if (index < 0 || index + 1 >= args.size())
    {
        print ("Uso: VstPlayer.exe --midi-send \"<puerto de salida>\" <msg> [<msg> ...]   (msg = 90:3C:64 o +ms)");
        return 2;
    }

    const auto portName = args[index + 1].unquoted();
    const auto devices = juce::MidiOutput::getAvailableDevices();
    juce::MidiDeviceInfo chosen;

    for (auto& device : devices)
        if (device.name == portName)
            chosen = device;

    if (chosen.identifier.isEmpty())
        for (auto& device : devices)
            if (device.name.containsIgnoreCase (portName))
                chosen = device;

    if (chosen.identifier.isEmpty())
    {
        print ("No existe el puerto de salida MIDI \"" + portName + "\"");
        return 3;
    }

    auto output = juce::MidiOutput::openDevice (chosen.identifier);

    if (output == nullptr)
    {
        print ("No se pudo abrir \"" + chosen.name + "\"");
        return 4;
    }

    int sent = 0;

    for (int i = index + 2; i < args.size(); ++i)
    {
        const auto token = args[i].unquoted().trim();

        if (token.startsWithChar ('+'))
        {
            juce::Thread::sleep (token.substring (1).getIntValue());
            continue;
        }

        juce::MemoryBlock bytes;

        for (auto& part : juce::StringArray::fromTokens (token, ":", {}))
        {
            const auto value = part.getHexValue32();

            if (part.isEmpty() || value < 0 || value > 255)
            {
                print ("Mensaje invalido: " + token);
                return 5;
            }

            const auto byte = (juce::uint8) value;
            bytes.append (&byte, 1);
        }

        if (bytes.isEmpty())
            continue;

        const juce::MidiMessage message (bytes.getData(), (int) bytes.getSize());
        output->sendMessageNow (message);
        ++sent;
    }

    print ("Enviados " + juce::String (sent) + " mensajes a \"" + chosen.name + "\"");
    return 0;
}


namespace
{
struct PrintingCallback final : public juce::MidiInputCallback
{
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override
    {
        const juce::ScopedLock sl (lock);
        lines.add (source->getName() + ": " + message.getDescription());
    }

    juce::CriticalSection lock;
    juce::StringArray lines;
};
} // namespace

int runMidiMonitor (const juce::StringArray& args)
{
    const auto index = args.indexOf ("--midi-monitor");
    const auto seconds = index >= 0 && index + 1 < args.size() ? juce::jmax (1, args[index + 1].getIntValue()) : 10;

    PrintingCallback callback;
    std::vector<std::unique_ptr<juce::MidiInput>> inputs;

    for (auto& device : juce::MidiInput::getAvailableDevices())
    {
        if (auto input = juce::MidiInput::openDevice (device.identifier, &callback))
        {
            input->start();
            print ("Escuchando " + device.name);
            inputs.push_back (std::move (input));
        }
        else
        {
            print ("No se pudo abrir " + device.name);
        }
    }

    print ("Monitoreando " + juce::String (seconds) + " s...");
    const auto end = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;

    while (juce::Time::getMillisecondCounterHiRes() < end)
    {
        juce::Thread::sleep (100);
        juce::StringArray pending;

        {
            const juce::ScopedLock sl (callback.lock);
            pending.swapWith (callback.lines);
        }

        for (auto& line : pending)
            print ("  " + line);
    }

    for (auto& input : inputs)
        input->stop();

    return 0;
}

} // namespace vstplayer::cli
