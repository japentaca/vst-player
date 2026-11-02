// Round-trip tests of the project model and its JSON serializer (registered in ctest).
// Exit code 0 = every check passed.

#include "project/Project.h"
#include "project/ProjectSerializer.h"

#include <iostream>

using namespace vstplayer::project;

namespace
{
int failures = 0;
int checks = 0;

void check (bool condition, const juce::String& what)
{
    ++checks;

    if (! condition)
    {
        ++failures;
        std::cout << "  FALLO " << what.toStdString() << std::endl;
    }
}

juce::MemoryBlock makeBinaryState (size_t size, int seed)
{
    juce::MemoryBlock block (size);
    juce::Random random (seed);

    for (size_t i = 0; i < size; ++i)
        static_cast<juce::uint8*> (block.getData())[i] = (juce::uint8) random.nextInt (256);

    return block;
}

Project makeSyntheticProject()
{
    Project p;
    p.name = juce::String::fromUTF8 ("Piano y pads — ñandú \"comillas\" \\ barra");
    p.audio.present = true;
    p.audio.deviceType = "Windows Audio";
    p.audio.outputDevice = "Altavoces (Focusrite USB Audio)";
    p.audio.sampleRate = 48000.0;
    p.audio.bufferSize = 256;

    InstanceData a;
    a.id = "1b4e28ba-2fa1-11d2-883f-0016d3cca427";
    a.name = "Piano";
    a.plugin.name = "sforzando";
    a.plugin.manufacturer = "Plogue";
    a.plugin.fileOrIdentifier = "C:\\Program Files\\Common Files\\VST3\\sforzando.vst3";
    a.plugin.uniqueId = "1a2b3c4d";
    a.plugin.version = "2.1.2";
    a.gainDb = -6.5;
    a.mute = false;
    a.state = serializer::encodeState (makeBinaryState (100000, 1));
    a.params = { { "Cutoff", 0.5 }, { "Volumen maestro", 0.25 } };
    a.window.hasPosition = true;
    a.window.x = -1200; // second monitor on the left
    a.window.y = 100;
    a.window.open = true;
    p.instances.push_back (a);

    InstanceData b;
    b.id = "9f0c1d2e-0000-4000-8000-000000000002";
    b.name = "Pads";
    b.plugin.name = "Surge XT";
    b.plugin.manufacturer = "Surge Synth Team";
    b.plugin.fileOrIdentifier = "C:\\Program Files\\Common Files\\VST3\\Surge Synth Team\\Surge XT.vst3";
    b.plugin.uniqueId = "deadbeef";
    b.plugin.version = "1.3.4";
    b.gainDb = -12.0;
    b.mute = true;
    b.state = serializer::encodeState (makeBinaryState (17, 2));
    b.window.open = false; // never opened: no position
    p.instances.push_back (b);

    MidiRouteData r1;
    r1.inputDevice = "loopMIDI Port";
    r1.channels = { 1, 2 };
    r1.instanceId = a.id;
    r1.remapTo = 0;
    p.midiRoutes.push_back (r1);

    MidiRouteData r2;
    r2.inputDevice = "loopMIDI Port";
    r2.channels = { 3, 4, 16 };
    r2.instanceId = b.id;
    r2.remapTo = 1;
    p.midiRoutes.push_back (r2);

    return p;
}

void checkProjectsEqual (const Project& x, const Project& y, const juce::String& label)
{
    check (x.version == y.version, label + ": version");
    check (x.name == y.name, label + ": name");
    check (x.audio.present == y.audio.present, label + ": audio presente");
    check (x.audio.deviceType == y.audio.deviceType && x.audio.outputDevice == y.audio.outputDevice
               && x.audio.sampleRate == y.audio.sampleRate && x.audio.bufferSize == y.audio.bufferSize,
           label + ": audio");
    check (x.instances.size() == y.instances.size(), label + ": cantidad de instancias");

    for (size_t i = 0; i < juce::jmin (x.instances.size(), y.instances.size()); ++i)
    {
        const auto& a = x.instances[i];
        const auto& b = y.instances[i];
        const auto l = label + ": instancia " + juce::String ((int) i);
        check (a.id == b.id && a.name == b.name, l + " id/nombre");
        check (a.plugin.name == b.plugin.name && a.plugin.manufacturer == b.plugin.manufacturer
                   && a.plugin.fileOrIdentifier == b.plugin.fileOrIdentifier && a.plugin.uniqueId == b.plugin.uniqueId
                   && a.plugin.version == b.plugin.version && a.plugin.format == b.plugin.format,
               l + " plugin");
        check (a.gainDb == b.gainDb && a.mute == b.mute, l + " gain/mute");
        check (a.state == b.state, l + " state");
        check (a.params.size() == b.params.size(), l + " cantidad de params");

        for (size_t k = 0; k < juce::jmin (a.params.size(), b.params.size()); ++k)
            check (a.params[k].name == b.params[k].name && a.params[k].value == b.params[k].value, l + " param " + a.params[k].name);

        check (a.window.hasPosition == b.window.hasPosition && a.window.x == b.window.x && a.window.y == b.window.y
                   && a.window.open == b.window.open,
               l + " ventana");
    }

    check (x.midiRoutes.size() == y.midiRoutes.size(), label + ": cantidad de rutas");

    for (size_t i = 0; i < juce::jmin (x.midiRoutes.size(), y.midiRoutes.size()); ++i)
    {
        const auto& a = x.midiRoutes[i];
        const auto& b = y.midiRoutes[i];
        check (a.inputDevice == b.inputDevice && a.channels == b.channels && a.instanceId == b.instanceId && a.remapTo == b.remapTo,
               label + ": ruta " + juce::String ((int) i));
    }
}

//==============================================================================
void testSyntheticRoundTrip()
{
    std::cout << "round-trip de un proyecto sintetico" << std::endl;
    const auto original = makeSyntheticProject();
    const auto json = serializer::toJson (original);

    Project parsed;
    juce::StringArray warnings;
    const auto result = serializer::fromJson (json, parsed, &warnings);
    check (result.wasOk(), "parsea: " + result.getErrorMessage());
    check (warnings.isEmpty(), "sin advertencias: " + warnings.joinIntoString (" | "));
    checkProjectsEqual (original, parsed, "sintetico");
    check (serializer::toJson (parsed) == json, "toJson (fromJson (x)) == x (texto identico)");

    // The binary state survives byte for byte.
    juce::MemoryBlock decoded;
    check (serializer::decodeState (parsed.instances[0].state, decoded), "decodifica el estado base64");
    check (decoded == makeBinaryState (100000, 1), "estado binario identico (100000 bytes)");

    // Standard base64 (not JUCE's MemoryBlock::toBase64Encoding format).
    const char hello[] = "hola";
    juce::MemoryBlock helloBlock (hello, 4);
    check (serializer::encodeState (helloBlock) == "aG9sYQ==", "base64 estandar");

    // Schema: keep / remapTo, channels, version, window without position.
    juce::var root;
    check (juce::JSON::parse (json, root).wasOk(), "JSON valido");
    check ((int) root["version"] == 1, "version 1");
    check (root["midiRoutes"][0]["channelMode"].toString() == "keep", "channelMode keep");
    check ((int) root["midiRoutes"][1]["channelMode"]["remapTo"] == 1, "channelMode { remapTo: 1 }");
    check (root["midiRoutes"][1]["channels"].size() == 3 && (int) root["midiRoutes"][1]["channels"][2] == 16, "channels [3, 4, 16]");
    check (! root["instances"][1]["window"].hasProperty ("x"), "ventana sin posicion no escribe x/y");
    check (! root["instances"][1].hasProperty ("params"), "params vacio no se escribe");
    check ((double) root["instances"][0]["params"]["Cutoff"] == 0.5, "params.Cutoff");
}

void testUnknownFieldsSurvive()
{
    std::cout << "campos desconocidos (version futura)" << std::endl;

    const auto text = juce::String::fromUTF8 (R"({
  "version": 3,
  "name": "Del futuro",
  "tempo": 120.5,
  "audio": { "deviceType": "ASIO", "outputDevice": "X", "sampleRate": 44100, "bufferSize": 64, "clockSource": "interno" },
  "instances": [
    {
      "id": "a", "name": "Uno",
      "plugin": { "name": "P", "manufacturer": "M", "format": "CLAP", "fileOrIdentifier": "p.clap", "uniqueId": "00000001", "version": "1", "sha256": "abc" },
      "gainDb": -3, "mute": true, "state": "",
      "params": { "Cutoff": 0.25 },
      "window": { "x": 10, "y": 20, "open": true, "monitor": 2 },
      "color": "#ff0000",
      "effects": [ { "type": "reverb", "mix": 0.3 } ]
    }
  ],
  "midiRoutes": [
    { "inputDevice": "loopMIDI Port", "channels": [1], "instanceId": "a", "channelMode": { "remapTo": 5 }, "velocityCurve": "soft" }
  ],
  "clock": { "source": "midi", "ppq": 24 },
  "notes": [ "uno", 2, null, true ]
})");

    Project p;
    juce::StringArray warnings;
    check (serializer::fromJson (text, p, &warnings).wasOk(), "parsea un proyecto version 3");
    check (warnings.size() == 1 && warnings[0].contains ("3"), "una sola advertencia (version nueva): " + warnings.joinIntoString (" | "));
    check (p.version == 3, "conserva version 3");
    check (p.instances.size() == 1 && p.instances[0].plugin.format == "CLAP", "instancia leida");

    const auto out = serializer::toJson (p);
    juce::var original, written;
    juce::JSON::parse (text, original);
    juce::JSON::parse (out, written);

    // Every property of the original is still there with the same value (deep equality).
    check (juce::JSON::toString (original["tempo"]) == juce::JSON::toString (written["tempo"]), "raiz.tempo");
    check (juce::JSON::toString (original["clock"]) == juce::JSON::toString (written["clock"]), "raiz.clock");
    check (juce::JSON::toString (original["notes"]) == juce::JSON::toString (written["notes"]), "raiz.notes");
    check (written["audio"]["clockSource"].toString() == juce::String::fromUTF8 ("interno"), "audio.clockSource");
    check (written["instances"][0]["plugin"]["sha256"].toString() == "abc", "plugin.sha256");
    check (written["instances"][0]["plugin"]["format"].toString() == "CLAP", "plugin.format");
    check ((int) written["instances"][0]["window"]["monitor"] == 2, "window.monitor");
    check (written["instances"][0]["color"].toString() == "#ff0000", "instancia.color");
    check (juce::JSON::toString (original["instances"][0]["effects"]) == juce::JSON::toString (written["instances"][0]["effects"]),
           "instancia.effects");
    check (written["midiRoutes"][0]["velocityCurve"].toString() == "soft", "ruta.velocityCurve");
    check ((int) written["midiRoutes"][0]["channelMode"]["remapTo"] == 5, "ruta remapTo 5");
    check ((int) written["version"] == 3, "version 3 se escribe igual");

    // And a second round-trip is stable.
    Project again;
    check (serializer::fromJson (out, again).wasOk() && serializer::toJson (again) == out, "segundo round-trip estable");
}

void testTolerantReading()
{
    std::cout << "lectura tolerante y errores" << std::endl;

    Project p;
    check (serializer::fromJson ("{ esto no es json", p).failed(), "JSON invalido -> error");
    check (serializer::fromJson ("[1, 2, 3]", p).failed(), "raiz que no es objeto -> error");

    juce::StringArray warnings;
    check (serializer::fromJson (R"({ "instances": [ { "id": "x", "gainDb": "fuerte", "plugin": { "name": "Dexed" } } ],
                                     "midiRoutes": [ { "inputDevice": "p", "instanceId": "x", "channels": [0, 2, 2, 17, 1], "channelMode": "remapTo:4" },
                                                     { "inputDevice": "p", "instanceId": "x", "channelMode": "raro" } ] })",
                                   p, &warnings)
               .wasOk(),
           "proyecto incompleto se lee");
    check (p.version == 1, "version por defecto 1");
    check (! p.audio.present, "sin audio");
    check (p.instances.size() == 1 && p.instances[0].gainDb == 0.0, "gainDb invalido -> 0");
    check (p.midiRoutes.size() == 2 && p.midiRoutes[0].channels == std::vector<int> { 1, 2 }, "canales filtrados y ordenados");
    check (p.midiRoutes[0].remapTo == 4, "\"remapTo:4\" aceptado");
    check (p.midiRoutes[1].channels.size() == 16, "sin channels -> todos");
    check (p.midiRoutes[1].remapTo == 0, "channelMode desconocido -> keep");
    check (warnings.size() == 4, "advertencias: " + juce::String (warnings.size()) + " -> " + warnings.joinIntoString (" | "));
    check (! serializer::toJson (p).contains ("\"audio\""), "sin audio no escribe audio");
}

void testFiles()
{
    std::cout << "archivos (escritura atomica)" << std::endl;

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("vstplayer-roundtrip-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    const auto file = dir.getChildFile (juce::String::fromUTF8 ("sub carpeta/Proyecto ñ.vstproj.json"));

    const auto original = makeSyntheticProject();
    auto result = serializer::saveFile (original, file);
    check (result.wasOk(), "guarda (crea la carpeta): " + result.getErrorMessage());

    Project loaded;
    result = serializer::loadFile (file, loaded);
    check (result.wasOk(), "carga: " + result.getErrorMessage());
    checkProjectsEqual (original, loaded, "archivo");

    // Overwrite the existing file: still atomic, no temporary files left behind.
    auto modified = original;
    modified.name = "Otro nombre";
    modified.instances.pop_back();
    check (serializer::saveFile (modified, file).wasOk(), "sobrescribe");
    check (serializer::loadFile (file, loaded).wasOk() && loaded.name == "Otro nombre" && loaded.instances.size() == 1,
           "lee la version nueva");
    check (file.getParentDirectory().getNumberOfChildFiles (juce::File::findFiles) == 1, "no quedan temporales en la carpeta");

    const auto text = file.loadFileAsString();
    check (! text.contains ("\r\n"), "fin de linea LF");

    check (serializer::loadFile (dir.getChildFile ("no-existe.vstproj.json"), loaded).failed(), "archivo inexistente -> error");

    check (nameFromFile (file) == juce::String::fromUTF8 ("Proyecto ñ"), "nameFromFile");
    check (withProjectSuffix (dir.getChildFile ("a")).getFileName() == "a.vstproj.json", "withProjectSuffix (sin extension)");
    check (withProjectSuffix (dir.getChildFile ("a.json")).getFileName() == "a.vstproj.json", "withProjectSuffix (.json)");
    check (withProjectSuffix (dir.getChildFile ("a.VSTPROJ.JSON")).getFileName() == "a.VSTPROJ.JSON", "withProjectSuffix (ya tiene)");

    dir.deleteRecursively();
}
void checkProjectFile (const juce::File& file, bool strict)
{
    const auto name = file.getFileName();
    Project p;
    juce::StringArray warnings;
    const auto result = serializer::loadFile (file, p, &warnings);
    check (result.wasOk(), name + ": " + result.getErrorMessage());

    if (strict)
    {
        check (warnings.isEmpty(), name + " sin advertencias: " + warnings.joinIntoString (" | "));
        check (! p.instances.empty() && ! p.midiRoutes.empty(), name + " tiene instancias y rutas");
    }

    Project again;
    check (serializer::fromJson (serializer::toJson (p), again).wasOk() && serializer::toJson (again) == serializer::toJson (p),
           name + " round-trip estable");
}

void testFixturesAndExamples()
{
    const juce::File sourceDir (VSTPLAYER_SOURCE_DIR);

    // tests/fixtures: the test projects (fixed contents, also used by the API and render tests).
    std::cout << "proyectos de prueba (tests/fixtures/)" << std::endl;
    const auto fixtures = sourceDir.getChildFile ("tests").getChildFile ("fixtures");
    for (auto name : { "prueba-etapa5.vstproj.json", "plugin-faltante.vstproj.json" })
        checkProjectFile (fixtures.getChildFile (name), true);

    // examples/: edited by hand or with the app, so only "loads and round-trips".
    std::cout << "proyectos de ejemplo (examples/)" << std::endl;
    for (const auto& file : sourceDir.getChildFile ("examples").findChildFiles (juce::File::findFiles, false, "*.vstproj.json"))
        checkProjectFile (file, false);
}
} // namespace

int main()
{
    testSyntheticRoundTrip();
    testUnknownFieldsSurvive();
    testTolerantReading();
    testFiles();
    testFixturesAndExamples();

    std::cout << (failures == 0 ? "OK" : "FALLO") << ": " << (checks - failures) << "/" << checks << " comprobaciones" << std::endl;
    return failures == 0 ? 0 : 1;
}
