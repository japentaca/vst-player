#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace vstplayer::project
{

// In-memory model of a *.vstproj.json file (schema: docs/project-format.md).
// Plain data, juce_core only (no UI, no audio). Every object keeps the JSON properties it
// does not know in `extra`, so a file written by a newer version survives a round-trip.

constexpr int kCurrentVersion = 1;
constexpr const char* kFileSuffix = ".vstproj.json";

struct PluginRef
{
    juce::String name;
    juce::String manufacturer;
    juce::String fileOrIdentifier;
    juce::String uniqueId; // juce::PluginDescription::uniqueId as 8 hex digits
    juce::String version;
    juce::String format = "VST3";
    juce::NamedValueSet extra;
};

struct WindowState
{
    bool hasPosition = false; // x / y present
    int x = 0;
    int y = 0;
    bool open = false;
    juce::NamedValueSet extra;
};

struct ParamOverride
{
    juce::String name; // parameter name as the plugin reports it (first match wins)
    double value = 0.0; // normalised 0..1
};

struct InstanceData
{
    juce::String id;
    juce::String name;
    PluginRef plugin;
    double gainDb = 0.0;
    bool mute = false;
    juce::String state; // standard base64 of getStateInformation (empty = none)
    std::vector<ParamOverride> params; // applied after the state; omitted from the JSON if empty
    WindowState window;
    juce::NamedValueSet extra;
};

struct AudioSetup
{
    bool present = false; // "audio" object present in the file
    juce::String deviceType;
    juce::String outputDevice;
    double sampleRate = 0.0;
    int bufferSize = 0;
    juce::NamedValueSet extra;
};

struct MidiRouteData
{
    juce::String inputDevice;
    std::vector<int> channels; // 1..16, ascending
    juce::String instanceId;
    int remapTo = 0; // 0 = "keep", 1..16 = { "remapTo": n }
    juce::NamedValueSet extra;
};

struct Project
{
    int version = kCurrentVersion;
    juce::String name;
    AudioSetup audio;
    std::vector<InstanceData> instances;
    std::vector<MidiRouteData> midiRoutes;
    juce::NamedValueSet extra;

    const InstanceData* findInstance (const juce::String& id) const;
};

// Helpers shared by the serializer and the app.
juce::uint16 channelsToMask (const std::vector<int>& channels);
std::vector<int> maskToChannels (juce::uint16 mask);

// "Mi proyecto.vstproj.json" -> "Mi proyecto".
juce::String nameFromFile (const juce::File& file);
// Appends ".vstproj.json" if the name does not end with it already.
juce::File withProjectSuffix (const juce::File& file);

} // namespace vstplayer::project
