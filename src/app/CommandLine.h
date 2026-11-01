#pragma once

#include <juce_core/juce_core.h>

namespace vstplayer
{

class AppSettings;

// Non-GUI modes of VstPlayer.exe. Each one runs to completion inside
// JUCEApplication::initialise() and returns the process exit code.
//
//   VstPlayer.exe --scan [--force]       scan the VST3 search path (out of process),
//                                        write plugins.xml, print the list and exit.
//                                        Without --force, files already known are skipped.
//   VstPlayer.exe --scan-one <file> <out.xml>
//                                        internal: child process of the out-of-process scanner.
//   VstPlayer.exe --audio-selftest [--type "<device type>"]
//                                        open the audio device, run the test tone at a very low
//                                        level (-40 dBFS), switch the buffer to 128 (or the
//                                        closest size the device offers) and check that the graph
//                                        is re-prepared. Does not modify settings.xml.
//   VstPlayer.exe --audio-devices        list audio device types, default output device,
//                                        sample rates and buffer sizes.
namespace cli
{
enum class Mode
{
    gui,
    scan,
    scanChild,
    audioSelfTest,
    audioDevices
};

Mode detectMode (const juce::StringArray& args);

// Writes a line to the parent console / redirected stdout (the app is a GUI-subsystem exe).
void print (const juce::String& line);

int runScan (const juce::StringArray& args, AppSettings& settings);
int runScanChild (const juce::StringArray& args);
int runAudioSelfTest (const juce::StringArray& args, AppSettings& settings);
int runAudioDevices();
} // namespace cli

} // namespace vstplayer
