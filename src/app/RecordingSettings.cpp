#include "app/RecordingSettings.h"

#include "project/ProjectController.h"

namespace vstplayer
{

namespace
{
constexpr const char* kFormatKey = "recordingFormat";
constexpr const char* kKeepWavKey = "recordingKeepWav";
constexpr const char* kFolderKey = "recordingFolder";
} // namespace

RecordingSettings RecordingSettings::load (const juce::PropertiesFile& properties)
{
    RecordingSettings s;
    s.format = RecordingFormat::fromKey (properties.getValue (kFormatKey, RecordingFormat { RecordingFormat::kDefault }.getKey()));
    s.keepWav = properties.getBoolValue (kKeepWavKey, false);
    s.folder = properties.getValue (kFolderKey).trim();
    return s;
}

void RecordingSettings::save (juce::PropertiesFile& properties) const
{
    properties.setValue (kFormatKey, format.getKey());
    properties.setValue (kKeepWavKey, keepWav);
    properties.setValue (kFolderKey, folder);
}

juce::File RecordingSettings::getFallbackFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("VstPlayer").getChildFile ("Grabaciones");
}

juce::File RecordingSettings::resolveFolder (const ProjectController& project) const
{
    if (folder.isNotEmpty() && juce::File::isAbsolutePath (folder))
        return juce::File (folder);

    if (project.hasFile())
        return project.getProjectFile().getParentDirectory();

    return getFallbackFolder();
}

Recorder::Options RecordingSettings::makeOptions (const ProjectController& project) const
{
    Recorder::Options options;
    options.folder = resolveFolder (project);
    options.baseName = project.getProjectName();
    options.format = format;
    options.keepWav = keepWav;
    return options;
}

} // namespace vstplayer
