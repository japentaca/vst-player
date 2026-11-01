#pragma once

#include "engine/InstanceGraph.h"
#include "engine/MidiRouter.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>
#include <map>
#include <memory>

namespace vstplayer
{

class AddInstanceDialog;
class MidiRouter;
class PluginManager;
class PluginEditorWindow;

// Central part of the main window (stage 6):
//   - Header: "Instancias (N)" and "Agregar instancia…" (opens AddInstanceDialog: search,
//     "sólo instrumentos", grouped by manufacturer).
//   - One row per instance (scrollable list). Line 1: editable name (double click), plugin /
//     state (red when missing or failed, progress bar while loading), stereo output meter
//     (measured in the instance's gain node), gain slider (dB), mute, "Editor", "Duplicar",
//     "Quitar". Line 2: MIDI activity LED, input port, channels (16 toggles or "todos") and
//     channel mode (keep / remap to n). The UI edits one route per instance. Clicking a row
//     selects it as the target of the virtual keyboard.
//   - Virtual keyboard at the bottom (channel 1, to the selected instance), foldable. A note-off
//     goes to the instance that received the note-on, even if the selection changed meanwhile.
// Owns the plugin editor windows (one per instance, brought to front if already open) and
// closes them before an instance is removed. Messages for the status bar go to onStatus.
class InstancePanel final : public juce::Component,
                            private InstanceGraph::Listener,
                            private juce::MidiKeyboardState::Listener,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    InstancePanel (InstanceGraph& graph, MidiRouter& router, PluginManager& plugins);
    ~InstancePanel() override;

    // Same paths as the buttons (also used by the automated self-tests).
    InstanceId addInstance (const juce::PluginDescription& description);
    void removeInstance (const InstanceId& id);
    // "Duplicar": same plugin, state, gain, mute and MIDI routes. Empty id if not possible.
    InstanceId duplicateInstance (const InstanceId& id);
    bool openEditor (const InstanceId& id);
    void closeEditor (const InstanceId& id);
    PluginEditorWindow* getEditorWindow (const InstanceId& id) const;
    // Copies the position of every open editor window into its instance (before saving).
    void syncEditorWindowStates();
    // Closes every editor (their positions are kept in the instances).
    void closeAllEditors();
    void selectInstance (const InstanceId& id);
    InstanceId getSelectedInstance() const { return selectedId; }
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }
    void panic();
    // Re-reads every row from the graph (gain, mute, name, state): changes made by the API.
    void refreshRows() { updateRows(); }

    // "Agregar instancia…" window (non-modal). getAddDialog() is null when closed.
    void showAddDialog();
    void closeAddDialog();
    AddInstanceDialog* getAddDialog() const noexcept { return addDialog.get(); }

    // Virtual keyboard fold state.
    void setKeyboardVisible (bool shouldBeVisible);
    bool isKeyboardVisible() const noexcept { return keyboardVisible; }

    // MIDI input of an instance (same paths as the row controls). The UI keeps one route per
    // instance: choosing a port creates it (all channels, keep), an empty name removes it.
    void setInstanceMidiPort (const InstanceId& id, const juce::String& portName);
    void setInstanceMidiChannels (const InstanceId& id, juce::uint16 channelMask);
    void setInstanceMidiRemap (const InstanceId& id, int remapChannel); // 0 = keep

    // What a row shows (tests).
    struct RowInfo
    {
        bool exists = false;
        juce::String port, channels, mode;
        int ledFlashes = 0; // times the activity LED lit up
        juce::String name;
        juce::String stateText; // plugin / state line
        bool shownAsMissing = false; // red row (plugin not installed)
        bool loadingIndicator = false; // progress bar visible
        float meterDb[2] { -100.0f, -100.0f }; // level shown by the stereo meter
        bool editorEnabled = false, duplicateEnabled = false;
    };

    RowInfo getRowInfo (const InstanceId& id) const;

    std::function<void (const juce::String& text, bool isError)> onStatus;
    std::function<void()> onShowPlugins;
    std::function<void (bool)> onKeyboardVisibilityChanged;

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
    void timerCallback() override;                                  // LEDs and meters

    void updateInstanceRoute (const InstanceId& id, const std::function<void (MidiRoute&)>& change);
    void showChannelSelector (const InstanceId& id, juce::Component& anchor);

    void updateRows();
    void updateKeyboardHeader();
    void setStatus (const juce::String& text, bool isError);

    InstanceGraph& graph;
    MidiRouter& router;
    PluginManager& plugins;

    juce::Label titleLabel;
    juce::TextButton addButton;

    std::unique_ptr<RowList> rowList;
    juce::Viewport viewport;

    juce::TextButton keyboardToggle;
    juce::Label keyboardLabel;
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard;
    bool keyboardVisible = true;

    InstanceId selectedId;
    std::map<int, InstanceId> heldNotes; // (channel << 8 | note) -> instance that got the note-on
    std::map<InstanceId, std::unique_ptr<PluginEditorWindow>> editorWindows;
    std::unique_ptr<AddInstanceDialog> addDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstancePanel)
};

} // namespace vstplayer
