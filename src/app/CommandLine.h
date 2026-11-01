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
//   VstPlayer.exe --midi-ports           list MIDI input and output ports.
//   VstPlayer.exe --midi-monitor [seconds]
//                                        open every MIDI input and print what arrives (default 10 s).
//   VstPlayer.exe --midi-send "<output port>" <msg> [<msg> ...]
//                                        send MIDI to an output port (e.g. "loopMIDI Port", whose
//                                        input then receives it). <msg> = hex bytes separated by
//                                        ':' (e.g. 90:3C:64 = note on ch 1, C3, vel 100) or +N to
//                                        wait N ms. Exit 0 if every message was sent.
namespace cli
{
enum class Mode
{
    gui,
    scan,
    scanChild,
    audioSelfTest,
    audioDevices,
    midiSend,
    midiPorts,
    midiMonitor
};

Mode detectMode (const juce::StringArray& args);

// Writes a line to the parent console / redirected stdout (the app is a GUI-subsystem exe).
void print (const juce::String& line);

// "--debug-to-stdout" (any mode): copies every OutputDebugString of the process (JUCE DBG,
// "JUCE Assertion failure in ...", the leak detector at exit, plugins' own messages) to stdout
// as "[debug] <text>". Without a debugger a jassert does not stop a Debug build, it only logs,
// so automated tests use this to catch assertions and leaks. Implemented with a vectored
// exception handler on DBG_PRINTEXCEPTION_C; stays active until the process ends.
void installDebugOutputCapture();

int runScan (const juce::StringArray& args, AppSettings& settings);
int runScanChild (const juce::StringArray& args);
int runAudioSelfTest (const juce::StringArray& args, AppSettings& settings);
int runAudioDevices();
int runMidiSend (const juce::StringArray& args);
int runMidiPorts();
int runMidiMonitor (const juce::StringArray& args);
} // namespace cli

} // namespace vstplayer
