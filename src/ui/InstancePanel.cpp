#include "ui/InstancePanel.h"

#include "engine/GainProcessor.h"
#include "engine/MidiRouter.h"
#include "engine/PluginManager.h"
#include "ui/PluginEditorWindow.h"

namespace vstplayer
{

namespace
{
constexpr int kRowHeight = 66;
constexpr double kLedHoldMs = 120.0;
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

        midiLabel.setText ("MIDI:", juce::dontSendNotification);
        midiLabel.setInterceptsMouseClicks (false, false);
        midiLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible (midiLabel);

        led.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (led);

        portBox.setTooltip ("Puerto de entrada MIDI");
        portBox.onChange = [this]
        {
            const auto selected = portBox.getSelectedId();

            if (selected == 1)
                owner.setInstanceMidiPort (id, {});
            else if (juce::isPositiveAndBelow (selected - 2, portItems.size()))
                owner.setInstanceMidiPort (id, portItems[selected - 2]);
        };
        addAndMakeVisible (portBox);

        channelsButton.setTooltip (juce::String::fromUTF8 ("Canales MIDI que escucha esta instancia"));
        channelsButton.onClick = [this] { owner.showChannelSelector (id, channelsButton); };
        addAndMakeVisible (channelsButton);

        modeBox.setTooltip ("keep: pasa el canal original; remapTo: lo cambia al canal elegido");
        modeBox.addItem ("Canal original (keep)", 1);
        for (int channel = 1; channel <= 16; ++channel)
            modeBox.addItem (juce::String::fromUTF8 ("Remapear → canal ") + juce::String (channel), channel + 1);
        modeBox.onChange = [this]
        {
            const auto selected = modeBox.getSelectedId();
            if (selected >= 1)
                owner.setInstanceMidiRemap (id, selected - 1);
        };
        addAndMakeVisible (modeBox);
    }

    // Activity LED: lit for a moment after each new routed message.
    class Led final : public juce::Component
    {
    public:
        void setLit (bool shouldBeLit)
        {
            if (lit != shouldBeLit)
            {
                lit = shouldBeLit;
                repaint();
            }
        }

        bool isLit() const noexcept { return lit; }

        void paint (juce::Graphics& g) override
        {
            auto area = getLocalBounds().toFloat().withSizeKeepingCentre (12.0f, 12.0f);
            g.setColour (lit ? juce::Colour (0xff4cff6a) : juce::Colour (0xff1f3a24));
            g.fillEllipse (area);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawEllipse (area, 1.0f);
        }

    private:
        bool lit = false;
    };

    void refreshMidi (const MidiRouter& router)
    {
        const auto routes = router.getRoutesForInstance (id);
        const auto* route = routes.empty() ? nullptr : &routes.front();
        const auto current = route != nullptr ? route->inputDevice : juce::String();

        auto items = router.getAvailableInputNames();
        const auto missing = current.isNotEmpty() && ! items.contains (current);

        if (missing)
            items.add (current);

        if (items != portItems || missing != portMissing)
        {
            portItems = items;
            portMissing = missing;
            portBox.clear (juce::dontSendNotification);
            portBox.addItem ("(sin entrada MIDI)", 1);

            for (int i = 0; i < portItems.size(); ++i)
                portBox.addItem (portItems[i] + (missing && i == portItems.size() - 1 ? " (desconectado)" : ""), i + 2);
        }

        portBox.setSelectedId (route != nullptr ? portItems.indexOf (current) + 2 : 1, juce::dontSendNotification);
        const auto portOk = route == nullptr || router.isPortOpen (current);
        portBox.setColour (juce::ComboBox::textColourId, portOk ? juce::Colours::white : juce::Colours::orangered);
        portBox.setTooltip (portOk ? juce::String ("Puerto de entrada MIDI")
                                   : juce::String::fromUTF8 ("El puerto no está disponible: se reabre solo cuando vuelva"));

        channelsButton.setButtonText ("Canales: " + (route != nullptr ? MidiRoute::describeChannels (route->channelMask) : juce::String ("-")));
        channelsButton.setEnabled (route != nullptr);
        modeBox.setSelectedId (route != nullptr ? (route->isKeep() ? 1 : route->remapChannel + 1) : 1, juce::dontSendNotification);
        modeBox.setEnabled (route != nullptr);
    }

    void updateLed (juce::int64 activityCount, double nowMs)
    {
        if (activityCount != lastActivity)
        {
            lastActivity = activityCount;
            lastActivityMs = nowMs;
            ++flashes;
        }

        led.setLit (nowMs - lastActivityMs < kLedHoldMs);
    }

    int getLedFlashes() const noexcept { return flashes; }
    juce::String getPortText() const { return portBox.getText(); }
    juce::String getChannelsText() const { return channelsButton.getButtonText(); }
    juce::String getModeText() const { return modeBox.getText(); }

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
        auto midiLine = area.removeFromBottom (26);
        area.removeFromBottom (2);

        midiLabel.setBounds (midiLine.removeFromLeft (40));
        led.setBounds (midiLine.removeFromLeft (20));
        midiLine.removeFromLeft (6);
        portBox.setBounds (midiLine.removeFromLeft (240));
        midiLine.removeFromLeft (6);
        channelsButton.setBounds (midiLine.removeFromLeft (220));
        midiLine.removeFromLeft (6);
        modeBox.setBounds (midiLine.removeFromLeft (210));

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

    juce::Label midiLabel;
    Led led;
    juce::ComboBox portBox;
    juce::TextButton channelsButton;
    juce::ComboBox modeBox;
    juce::StringArray portItems;
    bool portMissing = false;
    juce::int64 lastActivity = 0;
    double lastActivityMs = -1.0e9;
    int flashes = 0;

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
// Content of the call-out opened by "Canales: ...": 16 toggles plus "Todos" / "Ninguno".
// Every click applies right away through onChange.
class InstancePanel::ChannelSelector final : public juce::Component
{
public:
    ChannelSelector (juce::uint16 initialMask, std::function<void (juce::uint16)> changed)
        : onChange (std::move (changed))
    {
        for (int channel = 1; channel <= 16; ++channel)
        {
            auto* toggle = toggles.add (new juce::ToggleButton (juce::String (channel)));
            toggle->setToggleState ((initialMask & (1u << (channel - 1))) != 0, juce::dontSendNotification);
            toggle->onClick = [this] { readToggles(); };
            addAndMakeVisible (toggle);
        }

        allButton.setButtonText ("Todos");
        allButton.onClick = [this] { setMask (MidiRoute::kAllChannels); };
        addAndMakeVisible (allButton);

        noneButton.setButtonText ("Ninguno");
        noneButton.onClick = [this] { setMask (0); };
        addAndMakeVisible (noneButton);

        setSize (4 * 56 + 16, 4 * 28 + 44);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        auto buttons = area.removeFromBottom (26);
        allButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
        noneButton.setBounds (buttons.reduced (2, 0));
        area.removeFromBottom (6);

        for (int i = 0; i < toggles.size(); ++i)
            toggles[i]->setBounds (area.getX() + (i % 4) * 56, area.getY() + (i / 4) * 28, 56, 26);
    }

private:
    void readToggles()
    {
        juce::uint16 mask = 0;

        for (int i = 0; i < toggles.size(); ++i)
            if (toggles[i]->getToggleState())
                mask = (juce::uint16) (mask | (1u << i));

        onChange (mask);
    }

    void setMask (juce::uint16 mask)
    {
        for (int i = 0; i < toggles.size(); ++i)
            toggles[i]->setToggleState ((mask & (1u << i)) != 0, juce::dontSendNotification);

        readToggles();
    }

    std::function<void (juce::uint16)> onChange;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::TextButton allButton, noneButton;
};

//==============================================================================
InstancePanel::InstancePanel (InstanceGraph& g, MidiRouter& r, PluginManager& p)
    : graph (g),
      router (r),
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
    router.addChangeListener (this);

    updateRows();
    startTimerHz (30);
}

InstancePanel::~InstancePanel()
{
    stopTimer();
    router.removeChangeListener (this);
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
    router.panic(); // forgets the notes held through MIDI ports, then panics every instance
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
void InstancePanel::updateInstanceRoute (const InstanceId& id, const std::function<void (MidiRoute&)>& change)
{
    auto routes = router.getRoutesForInstance (id);

    if (routes.empty())
        return;

    routes.resize (1); // the provisional UI edits one route per instance
    change (routes.front());
    router.setRoutesForInstance (id, routes);
}

void InstancePanel::setInstanceMidiPort (const InstanceId& id, const juce::String& portName)
{
    if (portName.isEmpty())
    {
        router.setRoutesForInstance (id, {});
        return;
    }

    auto routes = router.getRoutesForInstance (id);

    if (routes.empty())
    {
        MidiRoute route;
        route.instanceId = id;
        route.channelMask = MidiRoute::kAllChannels;
        route.remapChannel = 0;
        routes.push_back (route);
    }

    routes.resize (1);
    routes.front().inputDevice = portName;
    router.setRoutesForInstance (id, routes);
}

void InstancePanel::setInstanceMidiChannels (const InstanceId& id, juce::uint16 channelMask)
{
    updateInstanceRoute (id, [channelMask] (MidiRoute& r) { r.channelMask = channelMask; });
}

void InstancePanel::setInstanceMidiRemap (const InstanceId& id, int remapChannel)
{
    updateInstanceRoute (id, [remapChannel] (MidiRoute& r) { r.remapChannel = juce::jlimit (0, 16, remapChannel); });
}

void InstancePanel::showChannelSelector (const InstanceId& id, juce::Component& anchor)
{
    const auto routes = router.getRoutesForInstance (id);

    if (routes.empty())
        return;

    juce::Component::SafePointer<InstancePanel> safeThis (this);
    auto selector = std::make_unique<ChannelSelector> (routes.front().channelMask, [safeThis, id] (juce::uint16 mask)
    {
        if (safeThis != nullptr)
            safeThis->setInstanceMidiChannels (id, mask);
    });

    juce::CallOutBox::launchAsynchronously (std::move (selector), anchor.getScreenBounds(), nullptr);
}

InstancePanel::RowInfo InstancePanel::getRowInfo (const InstanceId& id) const
{
    RowInfo info;

    for (auto* row : rowList->rows)
    {
        if (row->getId() == id)
        {
            info.exists = true;
            info.port = row->getPortText();
            info.channels = row->getChannelsText();
            info.mode = row->getModeText();
            info.ledFlashes = row->getLedFlashes();
        }
    }

    return info;
}

void InstancePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateRows();
}

void InstancePanel::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();

    for (auto* row : rowList->rows)
        row->updateLed (router.getActivityCount (row->getId()), now);
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
        rows[(int) i]->refreshMidi (router);
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
