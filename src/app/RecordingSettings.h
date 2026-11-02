#pragma once

#include "engine/Recorder.h"

#include <juce_data_structures/juce_data_structures.h>

namespace vstplayer
{

class ProjectController;

// Recording options in settings.xml:
//   recordingFormat  "cbr256" (default), "cbr320", "cbr192", "vbr0", "vbr2", "wav"
//   recordingKeepWav also keep a 32-bit float WAV next to the MP3 (default false)
//   recordingFolder  empty (default) = the folder of the project's .vstproj.json; a project that
//                    was never saved records into Documents\VstPlayer\Grabaciones
struct RecordingSettings
{
    RecordingFormat format;
    bool keepWav = false;
    juce::String folder; // empty = project folder

    static RecordingSettings load (const juce::PropertiesFile& properties);
    void save (juce::PropertiesFile& properties) const;

    // Folder a recording goes to now (project folder / fixed folder / Documents fallback).
    juce::File resolveFolder (const ProjectController& project) const;
    // Options for Recorder::start with the current project's name and folder.
    Recorder::Options makeOptions (const ProjectController& project) const;

    static juce::File getFallbackFolder(); // Documents\VstPlayer\Grabaciones
};

} // namespace vstplayer
