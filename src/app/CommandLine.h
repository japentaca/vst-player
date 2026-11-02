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
//   VstPlayer.exe --plugin-folders       list the VST3 search folders (default + extra).
//   VstPlayer.exe --add-plugin-folder <dir>
//                                        add an extra search folder (settings.xml) and scan the
//                                        search folders (new files only). Exit 0 if the folder was
//                                        added or was already searched, 1 otherwise.
//   VstPlayer.exe --remove-plugin-folder <dir>
//                                        remove an extra folder and the plugins found only there
//                                        from plugins.xml. Exit 0 if it was an extra folder.
//   VstPlayer.exe --audio-selftest [--type "<device type>"]
//                                        open the audio device with the saved setup and run the
//                                        (silent) graph: blocks processed per second, prepareToPlay
//                                        arguments of the output mixer, CPU; then switch the buffer
//                                        to 128 (or the closest size the device offers) and check that
//                                        the graph is re-prepared. No sound since stage 6 (the test
//                                        tone was removed). Does not modify settings.xml.
//   VstPlayer.exe --audio-devices        list audio device types, default output device,
//                                        sample rates and buffer sizes.
//   VstPlayer.exe --midi-ports           list MIDI input and output ports.
//   VstPlayer.exe --midi-monitor [seconds]
//                                        open every MIDI input and print what arrives (default 10 s).
//   VstPlayer.exe --midi-send "<output port>" <msg> [<msg> ...]
//                                        send MIDI to an output port (e.g. "loopMIDI Port", whose
//                                        input then receives it). <msg> = hex bytes separated by
//                                        ':' (e.g. 90:3C:64 = note on ch 1, C3, vel 100) or +N to
//                                        wait N ms. Exit 0 if every message was sent.
//   VstPlayer.exe --render <project.vstproj.json> --midi <file.mid> --out <file.wav> [options]
//                                        offline render of a project playing a Standard MIDI File
//                                        to a WAV, without an audio device (see OfflineRender.h).
//                                        Runs inside the message loop, not in runCommandLineMode.
namespace cli
{
enum class Mode
{
    gui,
    scan,
    scanChild,
    pluginFolders,
    addPluginFolder,
    removePluginFolder,
    audioSelfTest,
    audioDevices,
    midiSend,
    midiPorts,
    midiMonitor,
    render
};

Mode detectMode (const juce::StringArray& args);

// Writes a line to the parent console / redirected stdout (the app is a GUI-subsystem exe).
void print (const juce::String& line);
// Same for stderr (progress of --render, whose stdout carries only the JSON summary). When stderr
// is not redirected it goes to the parent console, if any; never to a redirected stdout.
void printError (const juce::String& line);

// "--debug-to-stdout" (any mode): copies every OutputDebugString of the process (JUCE DBG,
// "JUCE Assertion failure in ...", the leak detector at exit, plugins' own messages) to stdout
// as "[debug] <text>". Without a debugger a jassert does not stop a Debug build, it only logs,
// so automated tests use this to catch assertions and leaks. Implemented with a vectored
// exception handler on DBG_PRINTEXCEPTION_C; stays active until the process ends.
void installDebugOutputCapture();

int runScan (const juce::StringArray& args, AppSettings& settings);
int runScanChild (const juce::StringArray& args);
int runPluginFolders (const juce::StringArray& args, AppSettings& settings);
int runAudioSelfTest (const juce::StringArray& args, AppSettings& settings);
int runAudioDevices();
int runMidiSend (const juce::StringArray& args);
int runMidiPorts();
int runMidiMonitor (const juce::StringArray& args);
} // namespace cli

} // namespace vstplayer
