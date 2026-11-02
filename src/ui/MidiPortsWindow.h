#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace vstplayer
{

class MidiRouter;

// Audio > "Puertos MIDI…": diagnostic window. Lists every MIDI input port with its state
// (available, open, routes, messages received, open error) and the last messages received on
// the open ports (port, channel, description, how many instances got it, or "descartado" for
// system messages). "Escuchar todos los puertos" keeps every port open while the window is
// shown, to see traffic on ports no instance uses yet. Routes with no port get a line of their
// own, and MIDI routed by the API (`midi` without instanceId) shows up in the log as "(API)".
class MidiPortsWindow final : public juce::DocumentWindow
{
public:
    explicit MidiPortsWindow (MidiRouter& router);
    ~MidiPortsWindow() override;

    void closeButtonPressed() override;

    // Text currently shown (tests).
    juce::String getPortsText() const;
    juce::String getLogText() const;

private:
    class Content;
    MidiRouter& router;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiPortsWindow)
};

} // namespace vstplayer
