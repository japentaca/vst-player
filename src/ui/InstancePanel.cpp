#include "ui/InstancePanel.h"

#include "engine/GainProcessor.h"
#include "engine/PluginManager.h"
#include "ui/PluginEditorWindow.h"

namespace vstplayer
{

namespace
{
constexpr int kRowHeight = 36;
constexpr int kKeyboardHeight = 90;

juce::String describeState (const Instance& instance)
{
    switch (instance.state)
    {
        case Instance::State::loading: return instance.description.name + juce::String::fromUTF8 (" — cargando…");
        case Instance::State::failed:  return instance.description.name + juce::String::fromUTF8 (" — ERROR: ") + instance.errorMessage;
        case Instance::State::ready:   break;
    }

    return instance.description.name + juce::String::fromUTF8 (" — ") + instance.description.manufacturerName;
}
} // namespace

//==============================================================================
class InstancePanel::Row final : public juce::Component
{
public:
    Row (InstancePanel& o, const InstanceId& instanceId)
        : owner (o), id (instanceId)
    {
        nameLabel.setEditable (false, true, false);
        nameLabel.setTooltip ("Doble clic para renombrar");
        nameLabel.onTextChange = [this] { owner.graph.setName (id, nameLabel.getText()); };
        nameLabel.addMouseListener (this, false);
        addAndMakeVisible (nameLabel);

        stateLabel.setInterceptsMouseClicks (false, false);
        stateLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible (stateLabel);

        gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        gainSlider.setRange (GainProcessor::kMinGainDb, GainProcessor::kMaxGainDb, 0.1);
        gainSlider.setTextValueSuffix (" dB");
        gainSlider.setDoubleClickReturnValue (true, 0.0);
        gainSlider.onValueChange = [this] { owner.graph.setGainDb (id, (float) gainSlider.getValue()); };
        addAndMakeVisible (gainSlider);

        muteButton.setButtonText ("Mute");
        muteButton.onClick = [this] { owner.graph.setMute (id, muteButton.getToggleState()); };
        addAndMakeVisible (muteButton);

        editorButton.setButtonText ("Editor");
        editorButton.onClick = [this] { owner.openEditor (id); };
        addAndMakeVisible (editorButton);

        removeButton.setButtonText ("Quitar");
        // Deferred: removing destroys this row, which must not happen inside its own click.
        removeButton.onClick = [this]
        {
            juce::Component::SafePointer<InstancePanel> safeOwner (&owner);
            const auto idToRemove = id;
            juce::MessageManager::callAsync ([safeOwner, idToRemove]
            {
                if (safeOwner != nullptr)
                    safeOwner->removeInstance (idToRemove);
            });
        };
        addAndMakeVisible (removeButton);
    }

    void refresh (const Instance& instance, bool isSelected)
    {
        if (! nameLabel.isBeingEdited())
            nameLabel.setText (instance.name, juce::dontSendNotification);

        stateLabel.setText (describeState (instance), juce::dontSendNotification);
        stateLabel.setColour (juce::Label::textColourId,
                              instance.state == Instance::State::failed ? juce::Colours::orangered : juce::Colours::lightgrey);

        gainSlider.setValue (instance.gainDb, juce::dontSendNotification);
        muteButton.setToggleState (instance.mute, juce::dontSendNotification);

        const auto ready = instance.isReady();
        gainSlider.setEnabled (ready);
        muteButton.setEnabled (ready);
        editorButton.setEnabled (ready);

        if (selected != isSelected)
        {
            selected = isSelected;
            repaint();
        }
    }

    const InstanceId& getId() const noexcept { return id; }

    void mouseDown (const juce::MouseEvent&) override { owner.selectInstance (id); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().reduced (0, 1).toFloat();
        g.setColour (selected ? juce::Colour (0xff2d4f6e) : juce::Colour (0xff2a2a2a));
        g.fillRoundedRectangle (bounds, 4.0f);

        if (selected)
        {
            g.setColour (juce::Colours::skyblue);
            g.fillRect (bounds.removeFromLeft (4.0f));
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8, 4);
        nameLabel.setBounds (area.removeFromLeft (170));
        area.removeFromLeft (4);
        removeButton.setBounds (area.removeFromRight (72));
        area.removeFromRight (6);
        editorButton.setBounds (area.removeFromRight (72));
        area.removeFromRight (6);
        muteButton.setBounds (area.removeFromRight (64));
        area.removeFromRight (6);
        gainSlider.setBounds (area.removeFromRight (230));
        area.removeFromRight (6);
        stateLabel.setBounds (area);
    }

private:
    InstancePanel& owner;
    const InstanceId id;
    bool selected = false;

    juce::Label nameLabel;
    juce::Label stateLabel;
    juce::Slider gainSlider;
    juce::ToggleButton muteButton;
    juce::TextButton editorButton;
    juce::TextButton removeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Row)
};

//==============================================================================
class InstancePanel::RowList final : public juce::Component
{
public:
    juce::OwnedArray<Row> rows;

    void paint (juce::Graphics& g) override
    {
        if (rows.isEmpty())
        {
            g.setColour (juce::Colours::grey);
            g.drawText (juce::String::fromUTF8 ("Sin instancias. Usá \"Agregar instancia\"."),
                        getLocalBounds().reduced (8), juce::Justification::topLeft);
        }
    }

    void resized() override
    {
        int y = 0;

        for (auto* row : rows)
        {
            row->setBounds (0, y, getWidth(), kRowHeight);
            y += kRowHeight;
        }
    }
};

//==============================================================================
InstancePanel::InstancePanel (InstanceGraph& g, PluginManager& p)
    : graph (g),
      plugins (p),
      keyboard (keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    addButton.setButtonText (juce::String::fromUTF8 ("Agregar instancia…"));
    addButton.onClick = [this] { showAddMenu(); };
    addAndMakeVisible (addButton);

    panicButton.setButtonText (juce::String::fromUTF8 ("Pánico"));
    panicButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff8b1e1e));
    panicButton.onClick = [this] { panic(); };
    addAndMakeVisible (panicButton);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    keyboardLabel.setJustificationType (juce::Justification::centredLeft);
    keyboardLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (keyboardLabel);

    rowList = std::make_unique<RowList>();
    viewport.setViewedComponent (rowList.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    keyboard.setMidiChannel (kKeyboardChannel);
    keyboard.setAvailableRange (24, 108);
    keyboard.setLowestVisibleKey (48);
    addAndMakeVisible (keyboard);

    keyboardState.addListener (this);
    graph.addListener (this);

    updateRows();
}

InstancePanel::~InstancePanel()
{
    graph.removeListener (this);
    keyboardState.removeListener (this);

    // Editors go before the instances (InstanceGraph is destroyed after this panel).
    editorWindows.clear();

    viewport.setViewedComponent (nullptr, false);
    rowList = nullptr;
}

//==============================================================================
InstanceId InstancePanel::addInstance (const juce::PluginDescription& description)
{
    setStatus (juce::String::fromUTF8 ("Cargando ") + description.name + juce::String::fromUTF8 ("…"), false);
    return graph.addInstance (description);
}

void InstancePanel::removeInstance (const InstanceId& id)
{
    graph.removeInstance (id); // instanceAboutToBeRemoved closes the editor first
}

bool InstancePanel::openEditor (const InstanceId& id)
{
    auto* instance = graph.findInstance (id);

    if (instance == nullptr || ! instance->isReady())
        return false;

    if (auto it = editorWindows.find (id); it != editorWindows.end())
    {
        it->second->setVisible (true);
        it->second->toFront (true);
        return true;
    }

    auto* plugin = instance->getPlugin();

    if (plugin == nullptr)
        return false;

    auto window = std::make_unique<PluginEditorWindow> (*plugin, instance->name + juce::String::fromUTF8 (" — ") + instance->description.name);

    window->onCloseRequested = [this, id] { closeEditor (id); };

    const auto& state = instance->window;
    auto placed = false;

    if (state.hasPosition)
    {
        // Only reuse the saved position if it is still on some display.
        const auto titleArea = juce::Rectangle<int> (state.position.x, state.position.y, 100, 30);

        for (auto& display : juce::Desktop::getInstance().getDisplays().displays)
        {
            if (display.userBounds.toNearestInt().intersects (titleArea))
            {
                window->setTopLeftPosition (state.position);
                placed = true;
                break;
            }
        }
    }

    if (! placed)
    {
        // Cascade new windows a little so several editors do not stack exactly.
        const auto offset = 30 * (int) editorWindows.size();
        const auto anchor = getTopLevelComponent()->getScreenBounds().getCentre();
        window->setTopLeftPosition (anchor.x - window->getWidth() / 2 + offset, anchor.y - window->getHeight() / 2 + offset);
    }

    window->setVisible (true);
    window->toFront (true);

    auto newState = instance->window;
    newState.open = true;
    graph.setEditorWindowState (id, newState);

    editorWindows[id] = std::move (window);
    return true;
}

void InstancePanel::closeEditor (const InstanceId& id)
{
    const auto it = editorWindows.find (id);

    if (it == editorWindows.end())
        return;

    if (auto* instance = graph.findInstance (id))
    {
        Instance::EditorWindowState state;
        state.open = false;
        state.hasPosition = true;
        state.position = it->second->getPosition();
        graph.setEditorWindowState (id, state);
    }

    // Take the window out of the map first, then destroy it (may run from its close button:
    // nothing touches the window after this).
    auto window = std::move (it->second);
    editorWindows.erase (it);
    window = nullptr;
}

PluginEditorWindow* InstancePanel::getEditorWindow (const InstanceId& id) const
{
    const auto it = editorWindows.find (id);
    return it != editorWindows.end() ? it->second.get() : nullptr;
}

void InstancePanel::selectInstance (const InstanceId& id)
{
    selectedId = id;
    updateRows();
}

void InstancePanel::panic()
{
    keyboardState.allNotesOff (0); // releases the on-screen keys (note-offs go to their targets)
    heldNotes.clear();
    graph.panic();
    setStatus (juce::String::fromUTF8 ("Pánico: all notes off + all sound off en los 16 canales de cada instancia."), false);
}

//==============================================================================
void InstancePanel::instanceListChanged()
{
    updateRows();
}

void InstancePanel::instanceAboutToBeRemoved (const Instance& instance)
{
    editorWindows.erase (instance.id);

    for (auto it = heldNotes.begin(); it != heldNotes.end();)
        it = it->second == instance.id ? heldNotes.erase (it) : std::next (it);

    if (selectedId == instance.id)
        selectedId.clear();
}

void InstancePanel::instanceLoadFinished (const Instance& instance)
{
    if (instance.state == Instance::State::failed)
    {
        setStatus (instance.errorMessage, true);
        return;
    }

    setStatus (instance.name + juce::String::fromUTF8 (" cargado."), false);

    if (graph.findInstance (selectedId) == nullptr)
        selectedId = instance.id;
}

//==============================================================================
void InstancePanel::handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity)
{
    if (selectedId.isEmpty())
        return;

    if (graph.sendMidi (selectedId, juce::MidiMessage::noteOn (midiChannel, midiNoteNumber, velocity)))
        heldNotes[(midiChannel << 8) | midiNoteNumber] = selectedId;
}

void InstancePanel::handleNoteOff (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity)
{
    const auto key = (midiChannel << 8) | midiNoteNumber;
    const auto it = heldNotes.find (key);

    if (it == heldNotes.end())
        return;

    graph.sendMidi (it->second, juce::MidiMessage::noteOff (midiChannel, midiNoteNumber, velocity));
    heldNotes.erase (it);
}

//==============================================================================
void InstancePanel::showAddMenu()
{
    const auto types = plugins.getKnownPluginList().getTypes();

    if (types.isEmpty())
    {
        setStatus (juce::String::fromUTF8 ("No hay plugins conocidos: escaneá en Archivo > Plugins…"), true);
        return;
    }

    juce::PopupMenu menu;
    juce::KnownPluginList::addToMenu (menu, types, juce::KnownPluginList::sortByManufacturer);

    juce::Component::SafePointer<InstancePanel> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton),
                        [safeThis, types] (int result)
                        {
                            if (safeThis == nullptr || result == 0)
                                return;

                            const auto index = juce::KnownPluginList::getIndexChosenByMenu (types, result);

                            if (juce::isPositiveAndBelow (index, types.size()))
                                safeThis->addInstance (types.getReference (index));
                        });
}

void InstancePanel::updateRows()
{
    const auto& instances = graph.getInstances();
    auto& rows = rowList->rows;

    // Drop rows of instances that no longer exist.
    for (int i = rows.size(); --i >= 0;)
        if (graph.findInstance (rows[i]->getId()) == nullptr)
            rows.remove (i);

    // Keep row order == instance order, reusing existing rows (never recreate a row that may
    // be inside one of its own callbacks, e.g. a rename).
    for (size_t i = 0; i < instances.size(); ++i)
    {
        const auto& instance = *instances[i];
        int existing = -1;

        for (int r = 0; r < rows.size(); ++r)
            if (rows[r]->getId() == instance.id)
                existing = r;

        if (existing < 0)
        {
            auto* row = new Row (*this, instance.id);
            rowList->addAndMakeVisible (row);
            rows.insert ((int) i, row);
        }
        else if (existing != (int) i)
        {
            rows.move (existing, (int) i);
        }

        rows[(int) i]->refresh (instance, instance.id == selectedId);
    }

    rowList->setSize (viewport.getMaximumVisibleWidth(), juce::jmax (kRowHeight, rows.size() * kRowHeight));
    rowList->resized();
    rowList->repaint();

    const auto* selected = graph.findInstance (selectedId);
    keyboardLabel.setText (selected != nullptr
                               ? juce::String::fromUTF8 ("Teclado virtual (canal 1) → ") + selected->name
                               : juce::String::fromUTF8 ("Teclado virtual: elegí una instancia (clic en su fila)"),
                           juce::dontSendNotification);
}

void InstancePanel::setStatus (const juce::String& text, bool isError)
{
    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, isError ? juce::Colours::orangered : juce::Colours::lightgreen);
}

void InstancePanel::paint (juce::Graphics& g)
{
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
}

void InstancePanel::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (8);

    auto top = area.removeFromTop (30);
    addButton.setBounds (top.removeFromLeft (170));
    top.removeFromLeft (8);
    panicButton.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (12);
    statusLabel.setBounds (top);

    area.removeFromTop (6);
    keyboard.setBounds (area.removeFromBottom (kKeyboardHeight));
    keyboardLabel.setBounds (area.removeFromBottom (22));
    area.removeFromBottom (4);
    viewport.setBounds (area);

    updateRows();
}

} // namespace vstplayer
