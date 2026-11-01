#pragma once

#include "project/Project.h"

namespace vstplayer::project
{

// Project <-> JSON (juce::var / juce::JSON).
// - Known properties are written in a fixed order, then the unknown ones (`extra`) in the
//   order they were read.
// - Reading is tolerant: missing properties take their defaults, wrong types are reported as
//   warnings. Only a file that is not JSON, or whose root is not an object, is an error.
// - A file with "version" newer than kCurrentVersion is read (with a warning); its unknown
//   properties are kept.
namespace serializer
{
juce::var toVar (const Project& project);
juce::String toJson (const Project& project);

juce::Result fromVar (const juce::var& json, Project& out, juce::StringArray* warnings = nullptr);
juce::Result fromJson (const juce::String& text, Project& out, juce::StringArray* warnings = nullptr);

juce::Result loadFile (const juce::File& file, Project& out, juce::StringArray* warnings = nullptr);
// Atomic write: the JSON goes to a temporary file next to the target, which then replaces
// the target (rename). The target's folder is created if needed.
juce::Result saveFile (const Project& project, const juce::File& file);

// Standard base64 (RFC 4648), used for the plugin states.
juce::String encodeState (const juce::MemoryBlock& data);
bool decodeState (const juce::String& base64, juce::MemoryBlock& out);
} // namespace serializer

} // namespace vstplayer::project
