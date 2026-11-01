#pragma once

#include "engine/InstanceGraph.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <map>
#include <memory>

namespace vstplayer
{

class PluginManager;
class PluginEditorWindow;

// Provisional instance UI (stage 3, replaced in stage 6):
//   - "Agregar instancia" (popup of the KnownPluginList grouped by manufacturer), "Pánico".
//   - One row per instance: editable name, plugin / state, gain slider (dB), mute,
//     "Editor", "Quitar". Clicking a row selects it as the target of the virtual keyboard.
//   - Virtual keyboard at the bottom, channel 1, to the selected instance. A note-off goes to
//     the instance that received the note-on, even if the selection changed in between.
// Owns the plugin editor windows (one per instance, brought to front if already open) and
// closes them before an instance is removed.
class InstancePanel final : public juce::Component,
                            private InstanceGraph::Listener,
                            private juce::MidiKeyboardState::Listener
{
public:
    InstancePanel (InstanceGraph& graph, PluginManager& plugins);
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

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kKeyboardChannel = 1;

private:
    class Row;
    class RowList;

    // InstanceGraph::Listener
    void instanceListChanged() override;
    void instanceAboutToBeRemoved (const Instance&) override;
    void instanceLoadFinished (const Instance&) override;

    // MidiKeyboardState::Listener
    void handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;

    void showAddMenu();
    void updateRows();
    void setStatus (const juce::String& text, bool isError);

    InstanceGraph& graph;
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
