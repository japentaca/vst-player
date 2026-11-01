#include "project/ProjectSerializer.h"

namespace vstplayer::project::serializer
{

namespace
{
//==============================================================================
// Writing

void addExtra (juce::DynamicObject& object, const juce::NamedValueSet& extra)
{
    for (auto& property : extra)
        if (! object.hasProperty (property.name))
            object.setProperty (property.name, property.value);
}

juce::var pluginToVar (const PluginRef& plugin)
{
    auto object = juce::DynamicObject::Ptr (new juce::DynamicObject());
    object->setProperty ("name", plugin.name);
    object->setProperty ("manufacturer", plugin.manufacturer);
    object->setProperty ("format", plugin.format);
    object->setProperty ("fileOrIdentifier", plugin.fileOrIdentifier);
    object->setProperty ("uniqueId", plugin.uniqueId);
    object->setProperty ("version", plugin.version);
    addExtra (*object, plugin.extra);
    return juce::var (object.get());
}

juce::var windowToVar (const WindowState& window)
{
    auto object = juce::DynamicObject::Ptr (new juce::DynamicObject());

    if (window.hasPosition)
    {
        object->setProperty ("x", window.x);
        object->setProperty ("y", window.y);
    }

    object->setProperty ("open", window.open);
    addExtra (*object, window.extra);
    return juce::var (object.get());
}

juce::var instanceToVar (const InstanceData& instance)
{
    auto object = juce::DynamicObject::Ptr (new juce::DynamicObject());
    object->setProperty ("id", instance.id);
    object->setProperty ("name", instance.name);
    object->setProperty ("plugin", pluginToVar (instance.plugin));
    object->setProperty ("gainDb", instance.gainDb);
    object->setProperty ("mute", instance.mute);
    object->setProperty ("state", instance.state);

    if (! instance.params.empty())
    {
        auto params = juce::DynamicObject::Ptr (new juce::DynamicObject());

        for (auto& p : instance.params)
            params->setProperty (p.name, p.value);

        object->setProperty ("params", juce::var (params.get()));
    }

    object->setProperty ("window", windowToVar (instance.window));
    addExtra (*object, instance.extra);
    return juce::var (object.get());
}

juce::var audioToVar (const AudioSetup& audio)
{
    auto object = juce::DynamicObject::Ptr (new juce::DynamicObject());
    object->setProperty ("deviceType", audio.deviceType);
    object->setProperty ("outputDevice", audio.outputDevice);
    object->setProperty ("sampleRate", audio.sampleRate);
    object->setProperty ("bufferSize", audio.bufferSize);
    addExtra (*object, audio.extra);
    return juce::var (object.get());
}

juce::var routeToVar (const MidiRouteData& route)
{
    auto object = juce::DynamicObject::Ptr (new juce::DynamicObject());
    object->setProperty ("inputDevice", route.inputDevice);

    juce::Array<juce::var> channels;
    for (auto ch : route.channels)
        channels.add (ch);

    object->setProperty ("channels", channels);
    object->setProperty ("instanceId", route.instanceId);

    if (route.remapTo >= 1 && route.remapTo <= 16)
    {
        auto mode = juce::DynamicObject::Ptr (new juce::DynamicObject());
        mode->setProperty ("remapTo", route.remapTo);
        object->setProperty ("channelMode", juce::var (mode.get()));
    }
    else
    {
        object->setProperty ("channelMode", "keep");
    }

    addExtra (*object, route.extra);
    return juce::var (object.get());
}

//==============================================================================
// Reading

bool isMissing (const juce::var& v) { return v.isVoid() || v.isUndefined(); }
bool isNumber (const juce::var& v) { return v.isDouble() || v.isInt() || v.isInt64(); }

// Reads the properties of one JSON object: known ones through get*(), the rest end up in
// extra() (in their original order).
class Reader
{
public:
    Reader (const juce::var& v, juce::String objectPath, juce::StringArray* w)
        : object (v.getDynamicObject()), path (std::move (objectPath)), warnings (w)
    {
    }

    bool isObject() const noexcept { return object != nullptr; }

    bool has (const juce::Identifier& key) const { return object != nullptr && object->hasProperty (key); }

    juce::var get (const juce::Identifier& key)
    {
        known.add (key.toString());
        return object != nullptr ? object->getProperty (key) : juce::var();
    }

    juce::String getString (const juce::Identifier& key, const juce::String& fallback = {})
    {
        const auto v = get (key);

        if (isMissing (v))
            return fallback;

        if (! v.isString())
            warn (key, "se esperaba texto");

        return v.toString();
    }

    double getNumber (const juce::Identifier& key, double fallback)
    {
        const auto v = get (key);

        if (isMissing (v))
            return fallback;

        if (! isNumber (v))
        {
            warn (key, juce::String::fromUTF8 ("se esperaba un número"));
            return fallback;
        }

        return (double) v;
    }

    bool getBool (const juce::Identifier& key, bool fallback)
    {
        const auto v = get (key);

        if (isMissing (v))
            return fallback;

        if (! (v.isBool() || v.isInt() || v.isInt64()))
        {
            warn (key, "se esperaba true / false");
            return fallback;
        }

        return (bool) v;
    }

    juce::NamedValueSet extra() const
    {
        juce::NamedValueSet result;

        if (object != nullptr)
            for (auto& property : object->getProperties())
                if (! known.contains (property.name.toString()))
                    result.set (property.name, property.value);

        return result;
    }

    void warn (const juce::Identifier& key, const juce::String& what) const
    {
        if (warnings != nullptr)
            warnings->add (path + "." + key.toString() + ": " + what);
    }

private:
    juce::DynamicObject* object;
    juce::String path;
    juce::StringArray* warnings;
    juce::StringArray known;
};

PluginRef pluginFromVar (const juce::var& v, const juce::String& path, juce::StringArray* warnings)
{
    Reader r (v, path, warnings);
    PluginRef plugin;
    plugin.name = r.getString ("name");
    plugin.manufacturer = r.getString ("manufacturer");
    plugin.format = r.getString ("format", "VST3");
    plugin.fileOrIdentifier = r.getString ("fileOrIdentifier");
    plugin.uniqueId = r.getString ("uniqueId");
    plugin.version = r.getString ("version");
    plugin.extra = r.extra();
    return plugin;
}

WindowState windowFromVar (const juce::var& v, const juce::String& path, juce::StringArray* warnings)
{
    Reader r (v, path, warnings);
    WindowState window;
    window.hasPosition = r.has ("x") && r.has ("y");
    window.x = juce::roundToInt (r.getNumber ("x", 0.0));
    window.y = juce::roundToInt (r.getNumber ("y", 0.0));
    window.open = r.getBool ("open", false);
    window.extra = r.extra();
    return window;
}

InstanceData instanceFromVar (const juce::var& v, const juce::String& path, juce::StringArray* warnings)
{
    Reader r (v, path, warnings);
    InstanceData instance;

    if (! r.isObject() && warnings != nullptr)
        warnings->add (path + ": se esperaba un objeto");

    instance.id = r.getString ("id");
    instance.name = r.getString ("name");
    instance.plugin = pluginFromVar (r.get ("plugin"), path + ".plugin", warnings);
    instance.gainDb = r.getNumber ("gainDb", 0.0);
    instance.mute = r.getBool ("mute", false);
    instance.state = r.getString ("state");

    const auto params = r.get ("params");

    if (auto* object = params.getDynamicObject())
    {
        for (auto& p : object->getProperties())
        {
            if (isNumber (p.value))
                instance.params.push_back ({ p.name.toString(), juce::jlimit (0.0, 1.0, (double) p.value) });
            else if (warnings != nullptr)
                warnings->add (path + ".params." + p.name.toString() + juce::String::fromUTF8 (": se esperaba un número 0..1"));
        }
    }
    else if (! isMissing (params))
    {
        r.warn ("params", "se esperaba un objeto { \"nombre\": valor }");
    }

    instance.window = windowFromVar (r.get ("window"), path + ".window", warnings);
    instance.extra = r.extra();
    return instance;
}

AudioSetup audioFromVar (const juce::var& v, const juce::String& path, juce::StringArray* warnings)
{
    Reader r (v, path, warnings);
    AudioSetup audio;
    audio.present = r.isObject();
    audio.deviceType = r.getString ("deviceType");
    audio.outputDevice = r.getString ("outputDevice");
    audio.sampleRate = r.getNumber ("sampleRate", 0.0);
    audio.bufferSize = juce::roundToInt (r.getNumber ("bufferSize", 0.0));
    audio.extra = r.extra();
    return audio;
}

MidiRouteData routeFromVar (const juce::var& v, const juce::String& path, juce::StringArray* warnings)
{
    Reader r (v, path, warnings);
    MidiRouteData route;
    route.inputDevice = r.getString ("inputDevice");
    route.instanceId = r.getString ("instanceId");

    const auto channels = r.get ("channels");

    if (auto* array = channels.getArray())
    {
        for (auto& ch : *array)
        {
            const auto n = (int) ch;

            if (isNumber (ch) && n >= 1 && n <= 16)
                route.channels.push_back (n);
            else
                r.warn ("channels", "canal fuera de 1..16 ignorado: " + ch.toString());
        }

        route.channels = maskToChannels (channelsToMask (route.channels)); // sorted, unique
    }
    else if (isMissing (channels) || (channels.isString() && channels.toString().equalsIgnoreCase ("all")))
    {
        route.channels = maskToChannels (0xffff);
    }
    else
    {
        r.warn ("channels", "se esperaba una lista de canales");
    }

    const auto mode = r.get ("channelMode");

    if (auto* object = mode.getDynamicObject())
    {
        route.remapTo = juce::jlimit (0, 16, (int) object->getProperty ("remapTo"));
    }
    else if (mode.isString() && mode.toString().startsWithIgnoreCase ("remapTo"))
    {
        // Also accepted: "remapTo:3" / "remapTo 3".
        route.remapTo = juce::jlimit (0, 16, mode.toString().retainCharacters ("0123456789").getIntValue());
    }
    else if (! (isMissing (mode) || mode.toString() == "keep"))
    {
        r.warn ("channelMode", "valor desconocido \"" + mode.toString() + "\", se usa keep");
    }

    route.extra = r.extra();
    return route;
}
} // namespace

//==============================================================================
juce::var toVar (const Project& project)
{
    auto root = juce::DynamicObject::Ptr (new juce::DynamicObject());
    root->setProperty ("version", project.version);
    root->setProperty ("name", project.name);

    if (project.audio.present)
        root->setProperty ("audio", audioToVar (project.audio));

    juce::Array<juce::var> instances;
    for (auto& instance : project.instances)
        instances.add (instanceToVar (instance));

    root->setProperty ("instances", instances);

    juce::Array<juce::var> routes;
    for (auto& route : project.midiRoutes)
        routes.add (routeToVar (route));

    root->setProperty ("midiRoutes", routes);
    addExtra (*root, project.extra);
    return juce::var (root.get());
}

juce::String toJson (const Project& project)
{
    return juce::JSON::toString (toVar (project), juce::JSON::FormatOptions {}.withSpacing (juce::JSON::Spacing::multiLine));
}

juce::Result fromVar (const juce::var& json, Project& out, juce::StringArray* warnings)
{
    if (json.getDynamicObject() == nullptr)
        return juce::Result::fail (juce::String::fromUTF8 ("El archivo no contiene un objeto JSON de proyecto."));

    Reader r (json, "proyecto", warnings);
    Project project;

    project.version = juce::roundToInt (r.getNumber ("version", kCurrentVersion));

    if (project.version > kCurrentVersion && warnings != nullptr)
        warnings->add (juce::String::fromUTF8 ("El proyecto es de una versión más nueva del formato (")
                       + juce::String (project.version)
                       + juce::String::fromUTF8 ("); se abre igual y se conservan los campos desconocidos."));

    project.name = r.getString ("name");

    const auto audio = r.get ("audio");
    if (! isMissing (audio))
        project.audio = audioFromVar (audio, "audio", warnings);

    if (auto* instances = r.get ("instances").getArray())
        for (int i = 0; i < instances->size(); ++i)
            project.instances.push_back (instanceFromVar (instances->getReference (i), "instances[" + juce::String (i) + "]", warnings));

    if (auto* routes = r.get ("midiRoutes").getArray())
        for (int i = 0; i < routes->size(); ++i)
            project.midiRoutes.push_back (routeFromVar (routes->getReference (i), "midiRoutes[" + juce::String (i) + "]", warnings));

    project.extra = r.extra();
    out = std::move (project);
    return juce::Result::ok();
}

juce::Result fromJson (const juce::String& text, Project& out, juce::StringArray* warnings)
{
    juce::var json;
    const auto parsed = juce::JSON::parse (text, json);

    if (parsed.failed())
        return juce::Result::fail (juce::String::fromUTF8 ("JSON inválido: ") + parsed.getErrorMessage());

    return fromVar (json, out, warnings);
}

juce::Result loadFile (const juce::File& file, Project& out, juce::StringArray* warnings)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("No existe el archivo " + file.getFullPathName());

    juce::String text;

    {
        juce::FileInputStream in (file);

        if (! in.openedOk())
            return juce::Result::fail ("No se pudo leer " + file.getFullPathName() + ": " + in.getStatus().getErrorMessage());

        text = in.readEntireStreamAsString();
    }

    const auto result = fromJson (text, out, warnings);

    if (result.failed())
        return juce::Result::fail (file.getFileName() + ": " + result.getErrorMessage());

    return result;
}

juce::Result saveFile (const Project& project, const juce::File& file)
{
    const auto folder = file.getParentDirectory();

    if (! folder.isDirectory())
        if (const auto created = folder.createDirectory(); created.failed())
            return juce::Result::fail ("No se pudo crear la carpeta " + folder.getFullPathName() + ": " + created.getErrorMessage());

    const auto text = toJson (project) + "\n";

    // Written next to the target, then renamed over it (MoveFileEx replace on Windows): a
    // crash or a full disk never leaves a half-written project.
    juce::TemporaryFile temp (file);

    {
        juce::FileOutputStream out (temp.getFile());

        if (! out.openedOk())
            return juce::Result::fail ("No se pudo escribir " + temp.getFile().getFullPathName() + ": " + out.getStatus().getErrorMessage());

        if (! out.writeText (text, false, false, "\n"))
            return juce::Result::fail ("Error al escribir " + temp.getFile().getFullPathName());

        out.flush();

        if (out.getStatus().failed())
            return juce::Result::fail ("Error al escribir " + temp.getFile().getFullPathName() + ": " + out.getStatus().getErrorMessage());
    }

    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("No se pudo reemplazar " + file.getFullPathName()
                                   + juce::String::fromUTF8 (" (¿está abierto en otro programa o es de sólo lectura?)"));

    return juce::Result::ok();
}

juce::String encodeState (const juce::MemoryBlock& data)
{
    return juce::Base64::toBase64 (data.getData(), data.getSize());
}

bool decodeState (const juce::String& base64, juce::MemoryBlock& out)
{
    out.reset();
    juce::MemoryOutputStream stream (out, false);
    return juce::Base64::convertFromBase64 (stream, base64.removeCharacters (" \r\n\t"));
}

} // namespace vstplayer::project::serializer
