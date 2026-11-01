#pragma once

#include "engine/InstanceGraph.h"
#include "engine/MidiRouter.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>
#include <map>
#include <memory>

namespace vstplayer
{

class MidiRouter;
class PluginManager;
class PluginEditorWindow;

// Provisional instance UI (stage 3, replaced in stage 6):
//   - "Agregar instancia" (popup of the KnownPluginList grouped by manufacturer), "Pánico".
//   - One row per instance: editable name, plugin / state, gain slider (dB), mute,
//     "Editor", "Quitar". Clicking a row selects it as the target of the virtual keyboard.
//     Second line (stage 4): MIDI activity LED, input port, channels (16 toggles or "todos")
//     and channel mode (keep / remap to n). The UI edits one route per instance.
//   - Virtual keyboard at the bottom, channel 1, to the selected instance. A note-off goes to
//     the instance that received the note-on, even if the selection changed in between.
// Owns the plugin editor windows (one per instance, brought to front if already open) and
// closes them before an instance is removed.
class InstancePanel final : public juce::Component,
                            private InstanceGraph::Listener,
                            private juce::MidiKeyboardState::Listener,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    InstancePanel (InstanceGraph& graph, MidiRouter& router, PluginManager& plugins);
    ~InstancePanel() override;

    // Same paths as the buttons (also used by the automated self-test).
    InstanceId addInstance (const juce::PluginDescription& description);
    void removeInstance (const InstanceId& id);
    bool openEditor (const InstanceId& id);
    void closeEditor (const InstanceId& id);
    PluginEditorWindow* getEditorWindow (const InstanceId& id) const;
    void selectInstance (const InstanceId& id);
    InstanceId getSelectedInstance() const { return selectedId; }
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }
    void panic();

    // MIDI input of an instance (same paths as the row controls). The UI keeps one route per
    // instance: choosing a port creates it (all channels, keep), an empty name removes it.
    void setInstanceMidiPort (const InstanceId& id, const juce::String& portName);
    void setInstanceMidiChannels (const InstanceId& id, juce::uint16 channelMask);
    void setInstanceMidiRemap (const InstanceId& id, int remapChannel); // 0 = keep

    // What a row shows for its MIDI input (tests).
    struct RowInfo
    {
        bool exists = false;
        juce::String port, channels, mode;
        int ledFlashes = 0; // times the activity LED lit up
    };

    RowInfo getRowInfo (const InstanceId& id) const;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kKeyboardChannel = 1;

private:
    class Row;
    class RowList;
    class ChannelSelector;

    // InstanceGraph::Listener
    void instanceListChanged() override;
    void instanceAboutToBeRemoved (const Instance&) override;
    void instanceLoadFinished (const Instance&) override;

    // MidiKeyboardState::Listener
    void handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override; // router: ports / routes
    void timerCallback() override;                                  // MIDI activity LEDs

    void updateInstanceRoute (const InstanceId& id, const std::function<void (MidiRoute&)>& change);
    void showChannelSelector (const InstanceId& id, juce::Component& anchor);

    void showAddMenu();
    void updateRows();
    void setStatus (const juce::String& text, bool isError);

    InstanceGraph& graph;
    MidiRouter& router;
    PluginManager& plugins;

    juce::TextButton addButton;
    juce::TextButton panicButton;
    juce::Label statusLabel;
    juce::Label keyboardLabel;

    std::unique_ptr<RowList> rowList;
    juce::Viewport viewport;

    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard;

    InstanceId selectedId;
    std::map<int, InstanceId> heldNotes; // (channel << 8 | note) -> instance that got the note-on
    std::map<InstanceId, std::unique_ptr<PluginEditorWindow>> editorWindows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstancePanel)
};

} // namespace vstplayer
