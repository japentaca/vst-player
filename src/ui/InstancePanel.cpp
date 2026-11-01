#include "ui/InstancePanel.h"

#include "engine/GainProcessor.h"
#include "engine/MidiRouter.h"
#include "engine/PluginManager.h"
#include "ui/AddInstanceDialog.h"
#include "ui/LevelMeter.h"
#include "ui/PluginEditorWindow.h"
#include "ui/Theme.h"

namespace vstplayer
{

namespace
{
constexpr int kRowHeight = 64;
constexpr int kRowGap = 4;
constexpr double kLedHoldMs = 120.0;
constexpr int kKeyboardHeight = 84;
constexpr int kHeaderHeight = 34;
constexpr int kKeyboardHeaderHeight = 26;

juce::String describeState (const Instance& instance)
{
    switch (instance.state)
    {
        case Instance::State::loading: return instance.description.name + juce::String::fromUTF8 (" — cargando…");
        case Instance::State::failed:  return instance.description.name + juce::String::fromUTF8 (" — ERROR: ") + instance.errorMessage;
        case Instance::State::missing: return instance.description.name + juce::String::fromUTF8 (" — FALTA: ") + instance.errorMessage;
        case Instance::State::ready:   break;
    }

    return instance.description.name + juce::String::fromUTF8 (" — ") + instance.description.manufacturerName;
}

// Activity LED: lit for a moment after each new routed message.
class Led final : public juce::Component
{
public:
    Led() { setInterceptsMouseClicks (false, false); }

    void setLit (bool shouldBeLit)
    {
        if (lit != shouldBeLit)
        {
            lit = shouldBeLit;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().withSizeKeepingCentre (11.0f, 11.0f);
        g.setColour (lit ? theme::ok : juce::Colour (0xff1f3324));
        g.fillEllipse (area);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (area, 1.0f);
    }

private:
    bool lit = false;
};
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
        nameLabel.setFont (juce::FontOptions (15.5f, juce::Font::bold));
        nameLabel.setMinimumHorizontalScale (0.8f);
        nameLabel.onTextChange = [this] { owner.graph.setName (id, nameLabel.getText()); };
        nameLabel.addMouseListener (this, false);
        addAndMakeVisible (nameLabel);

        stateLabel.setInterceptsMouseClicks (false, false);
        stateLabel.setColour (juce::Label::textColourId, theme::textDim);
        stateLabel.setMinimumHorizontalScale (0.9f);
        addAndMakeVisible (stateLabel);

        addChildComponent (loadingBar);
        loadingBar.setTextToDisplay (juce::String::fromUTF8 ("cargando…"));

        addAndMakeVisible (meter);

        gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
        gainSlider.setRange (GainProcessor::kMinGainDb, GainProcessor::kMaxGainDb, 0.1);
        gainSlider.setTextValueSuffix (" dB");
        gainSlider.setDoubleClickReturnValue (true, 0.0);
        gainSlider.setTooltip (juce::String::fromUTF8 ("Ganancia (doble clic: 0 dB)"));
        gainSlider.onValueChange = [this] { owner.graph.setGainDb (id, (float) gainSlider.getValue()); };
        addAndMakeVisible (gainSlider);

        muteButton.setButtonText ("M");
        muteButton.setTooltip ("Mute");
        muteButton.setClickingTogglesState (true);
        muteButton.setColour (juce::TextButton::buttonOnColourId, theme::mute);
        muteButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        muteButton.onClick = [this] { owner.graph.setMute (id, muteButton.getToggleState()); };
        addAndMakeVisible (muteButton);

        editorButton.setButtonText ("Editor");
        editorButton.setTooltip ("Abrir la interfaz del plugin");
        editorButton.onClick = [this] { owner.openEditor (id); };
        addAndMakeVisible (editorButton);

        duplicateButton.setButtonText ("Duplicar");
        duplicateButton.setTooltip (juce::String::fromUTF8 ("Otra instancia con el mismo plugin, estado, ganancia y entrada MIDI"));
        duplicateButton.onClick = [this] { owner.duplicateInstance (id); };
        addAndMakeVisible (duplicateButton);

        removeButton.setButtonText ("Quitar");
        removeButton.setTooltip ("Quitar esta instancia");
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

        midiLabel.setText ("MIDI", juce::dontSendNotification);
        midiLabel.setInterceptsMouseClicks (false, false);
        midiLabel.setColour (juce::Label::textColourId, theme::textDim);
        midiLabel.setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (midiLabel);

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
        portBox.setColour (juce::ComboBox::textColourId, portOk ? theme::text : theme::error);
        portBox.setTooltip (portOk ? juce::String ("Puerto de entrada MIDI")
                                   : juce::String::fromUTF8 ("El puerto no está disponible: se reabre solo cuando vuelva"));

        channelsButton.setButtonText ("Canales: " + (route != nullptr ? MidiRoute::describeChannels (route->channelMask) : juce::String ("-")));
        channelsButton.setEnabled (route != nullptr);
        modeBox.setSelectedId (route != nullptr ? (route->isKeep() ? 1 : route->remapChannel + 1) : 1, juce::dontSendNotification);
        modeBox.setEnabled (route != nullptr);
    }

    void tick (const Instance* instance, juce::int64 activityCount, double nowMs)
    {
        if (activityCount != lastActivity)
        {
            lastActivity = activityCount;
            lastActivityMs = nowMs;
            ++flashes;
        }

        led.setLit (nowMs - lastActivityMs < kLedHoldMs);

        auto* gain = instance != nullptr ? instance->getGainProcessor() : nullptr;

        if (gain != nullptr)
            meter.pushPeaks (gain->getAndResetMeterPeak (0), gain->getAndResetMeterPeak (1));
        else
            meter.pushPeaks (0.0f, 0.0f);
    }

    int getLedFlashes() const noexcept { return flashes; }
    juce::String getStateText() const { return stateLabel.getText(); }
    juce::String getNameText() const { return nameLabel.getText(); }
    bool isShownAsMissing() const noexcept { return missingPlugin; }
    bool isLoadingIndicatorVisible() const { return loadingBar.isVisible(); }
    juce::String getPortText() const { return portBox.getText(); }
    juce::String getChannelsText() const { return channelsButton.getButtonText(); }
    juce::String getModeText() const { return modeBox.getText(); }
    float getMeterDb (int channel) const { return meter.getLevelDb (channel); }
    bool isEditorEnabled() const { return editorButton.isEnabled(); }
    bool isDuplicateEnabled() const { return duplicateButton.isEnabled(); }

    void refresh (const Instance& instance, bool isSelected)
    {
        if (! nameLabel.isBeingEdited())
            nameLabel.setText (instance.name, juce::dontSendNotification);

        stateLabel.setText (describeState (instance), juce::dontSendNotification);
        const auto isProblem = instance.state == Instance::State::failed || instance.state == Instance::State::missing;
        stateLabel.setColour (juce::Label::textColourId, isProblem ? theme::error : theme::textDim);
        stateLabel.setTooltip (instance.description.fileOrIdentifier
                               + (instance.errorMessage.isNotEmpty() ? "\n" + instance.errorMessage : juce::String()));

        if (missingPlugin != isProblem)
        {
            missingPlugin = isProblem;
            repaint();
        }

        const auto loading = instance.state == Instance::State::loading;
        loadingBar.setVisible (loading);
        meter.setVisible (! loading);

        gainSlider.setValue (instance.gainDb, juce::dontSendNotification);
        muteButton.setToggleState (instance.mute, juce::dontSendNotification);

        const auto ready = instance.isReady();
        gainSlider.setEnabled (ready);
        muteButton.setEnabled (ready);
        editorButton.setEnabled (ready);
        duplicateButton.setEnabled (ready);

        if (! ready)
            meter.reset();

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
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (missingPlugin ? theme::rowMissing : selected ? theme::rowSelected : theme::row);
        g.fillRoundedRectangle (bounds, 5.0f);

        g.setColour (missingPlugin ? theme::error : selected ? theme::accent.withAlpha (0.7f) : theme::outline);
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

        if (selected)
        {
            g.setColour (theme::accent);
            g.fillRoundedRectangle (bounds.removeFromLeft (4.0f), 2.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 5);
        auto line1 = area.removeFromTop (26);
        area.removeFromTop (3);
        auto line2 = area.removeFromTop (24);

        // Line 1: name | plugin ... meter | gain | M | Editor | Duplicar | Quitar
        const auto width = line1.getWidth();
        const auto nameWidth = juce::jlimit (110, 220, width / 6);
        nameLabel.setBounds (line1.removeFromLeft (nameWidth));
        line1.removeFromLeft (6);

        removeButton.setBounds (line1.removeFromRight (66));
        line1.removeFromRight (6);
        duplicateButton.setBounds (line1.removeFromRight (76));
        line1.removeFromRight (6);
        editorButton.setBounds (line1.removeFromRight (64));
        line1.removeFromRight (10);
        muteButton.setBounds (line1.removeFromRight (30));
        line1.removeFromRight (6);
        gainSlider.setBounds (line1.removeFromRight (juce::jlimit (150, 240, width / 5)));
        line1.removeFromRight (6);
        const auto meterArea = line1.removeFromRight (juce::jlimit (80, 160, width / 9)).withSizeKeepingCentre (juce::jlimit (80, 160, width / 9), 14);
        meter.setBounds (meterArea);
        loadingBar.setBounds (meterArea.withHeight (18).withCentre (meterArea.getCentre()));
        line1.removeFromRight (8);
        stateLabel.setBounds (line1);

        // Line 2: MIDI (led) | port | channels | mode
        midiLabel.setBounds (line2.removeFromLeft (38));
        led.setBounds (line2.removeFromLeft (16));
        line2.removeFromLeft (6);
        const auto comboWidth = juce::jlimit (170, 300, line2.getWidth() / 3);
        portBox.setBounds (line2.removeFromLeft (comboWidth));
        line2.removeFromLeft (6);
        channelsButton.setBounds (line2.removeFromLeft (juce::jlimit (160, 260, line2.getWidth() / 3)));
        line2.removeFromLeft (6);
        modeBox.setBounds (line2.removeFromLeft (juce::jmin (210, line2.getWidth())));
    }

private:
    InstancePanel& owner;
    const InstanceId id;
    bool selected = false;
    bool missingPlugin = false;

    juce::Label nameLabel;
    juce::Label stateLabel;
    double loadingProgress = -1.0; // indeterminate
    juce::ProgressBar loadingBar { loadingProgress };
    LevelMeter meter;
    juce::Slider gainSlider;
    juce::TextButton muteButton;
    juce::TextButton editorButton;
    juce::TextButton duplicateButton;
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
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions (15.0f));
            g.drawFittedText (juce::String::fromUTF8 ("Sin instancias.\nUsá \"Agregar instancia…\" o abrí un proyecto (Ctrl+O)."),
                              getLocalBounds().reduced (12), juce::Justification::centred, 3);
        }
    }

    void resized() override
    {
        int y = 0;

        for (auto* row : rows)
        {
            row->setBounds (0, y, getWidth(), kRowHeight);
            y += kRowHeight + kRowGap;
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
    titleLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (titleLabel);

    addButton.setButtonText (juce::String::fromUTF8 ("+ Agregar instancia…"));
    addButton.setTooltip (juce::String::fromUTF8 ("Elegir un plugin de la lista (búsqueda, sólo instrumentos, por fabricante)"));
    addButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.45f));
    addButton.onClick = [this] { showAddDialog(); };
    addAndMakeVisible (addButton);

    rowList = std::make_unique<RowList>();
    viewport.setViewedComponent (rowList.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    keyboardToggle.setTooltip ("Mostrar u ocultar el teclado virtual");
    keyboardToggle.onClick = [this] { setKeyboardVisible (! keyboardVisible); };
    addAndMakeVisible (keyboardToggle);

    keyboardLabel.setJustificationType (juce::Justification::centredLeft);
    keyboardLabel.setColour (juce::Label::textColourId, theme::textDim);
    keyboardLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (keyboardLabel);

    keyboard.setMidiChannel (kKeyboardChannel);
    keyboard.setAvailableRange (24, 108);
    keyboard.setLowestVisibleKey (48);
    keyboard.setKeyWidth (22.0f);
    keyboard.setScrollButtonWidth (24);
    addAndMakeVisible (keyboard);

    keyboardState.addListener (this);
    graph.addListener (this);
    router.addChangeListener (this);

    updateRows();
    updateKeyboardHeader();
    startTimerHz (30);
}

InstancePanel::~InstancePanel()
{
    stopTimer();
    router.removeChangeListener (this);
    graph.removeListener (this);
    keyboardState.removeListener (this);

    addDialog = nullptr;
    // Editors go before the instances (InstanceGraph is destroyed after this panel).
    editorWindows.clear();

    viewport.setViewedComponent (nullptr, false);
    rowList = nullptr;
}

//==============================================================================
InstanceId InstancePanel::addInstance (const juce::PluginDescription& description)
{
    setStatus (juce::String::fromUTF8 ("Cargando ") + description.name + juce::String::fromUTF8 ("…"), false);
    const auto id = graph.addInstance (description);

    // The new row is the keyboard target and is scrolled into view.
    selectInstance (id);
    viewport.setViewPosition (0, juce::jmax (0, rowList->getHeight() - viewport.getMaximumVisibleHeight()));
    return id;
}

void InstancePanel::removeInstance (const InstanceId& id)
{
    graph.removeInstance (id); // instanceAboutToBeRemoved closes the editor first
}

InstanceId InstancePanel::duplicateInstance (const InstanceId& sourceId)
{
    const auto* source = graph.findInstance (sourceId);

    if (source == nullptr || ! source->isReady())
        return {};

    const auto sourceName = source->name;
    const auto newId = graph.duplicateInstance (sourceId);

    if (newId.isEmpty())
        return {};

    // Same MIDI input as the original.
    auto routes = router.getRoutesForInstance (sourceId);

    for (auto& route : routes)
        route.instanceId = newId;

    if (! routes.empty())
        router.setRoutesForInstance (newId, routes);

    setStatus (juce::String::fromUTF8 ("Duplicando ") + sourceName + juce::String::fromUTF8 ("…"), false);
    selectInstance (newId);
    viewport.setViewPosition (0, juce::jmax (0, rowList->getHeight() - viewport.getMaximumVisibleHeight()));
    return newId;
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

void InstancePanel::syncEditorWindowStates()
{
    for (auto& [id, window] : editorWindows)
    {
        Instance::EditorWindowState state;
        state.open = true;
        state.hasPosition = true;
        state.position = window->getPosition();
        graph.setEditorWindowState (id, state);
    }
}

void InstancePanel::closeAllEditors()
{
    while (! editorWindows.empty())
        closeEditor (editorWindows.begin()->first);
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
void InstancePanel::showAddDialog()
{
    if (addDialog != nullptr)
    {
        addDialog->setVisible (true);
        addDialog->toFront (true);
        return;
    }

    addDialog = std::make_unique<AddInstanceDialog> (plugins.getKnownPluginList(), getTopLevelComponent());

    juce::Component::SafePointer<InstancePanel> safeThis (this);
    addDialog->onAdd = [safeThis] (const juce::PluginDescription& description)
    {
        if (safeThis != nullptr)
            safeThis->addInstance (description);
    };
    addDialog->onClose = [safeThis]
    {
        // Deferred: the dialog may be inside one of its own callbacks.
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr && safeThis->addDialog != nullptr && ! safeThis->addDialog->isVisible())
                safeThis->addDialog = nullptr;
        });
    };
    addDialog->onShowPlugins = [safeThis]
    {
        if (safeThis != nullptr && safeThis->onShowPlugins)
            safeThis->onShowPlugins();
    };
}

void InstancePanel::closeAddDialog()
{
    addDialog = nullptr;
}

void InstancePanel::setKeyboardVisible (bool shouldBeVisible)
{
    if (keyboardVisible == shouldBeVisible)
        return;

    if (! shouldBeVisible)
        keyboardState.allNotesOff (0); // nothing stays held under a hidden keyboard

    keyboardVisible = shouldBeVisible;
    keyboard.setVisible (keyboardVisible);
    updateKeyboardHeader();
    resized();

    if (onKeyboardVisibilityChanged)
        onKeyboardVisibilityChanged (keyboardVisible);
}

void InstancePanel::updateKeyboardHeader()
{
    keyboardToggle.setButtonText (keyboardVisible ? juce::String::fromUTF8 ("▾ Teclado") : juce::String::fromUTF8 ("▸ Teclado"));

    const auto* selected = graph.findInstance (selectedId);
    keyboardLabel.setText (selected != nullptr
                               ? juce::String::fromUTF8 ("canal 1 → ") + selected->name
                                     + (keyboardVisible ? juce::String::fromUTF8 ("   (clic en el teclado y usá A W S E D F… para tocar con la PC)")
                                                        : juce::String())
                               : juce::String::fromUTF8 ("elegí una instancia (clic en su fila) para tocarla"),
                           juce::dontSendNotification);
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
    if (instance.state == Instance::State::failed || instance.state == Instance::State::missing)
    {
        setStatus (instance.errorMessage, true);
        return;
    }

    setStatus (instance.name + juce::String::fromUTF8 (" cargado (") + juce::String (juce::roundToInt (graph.getLastCreationMs()))
                   + " ms).",
               false);

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

    routes.resize (1); // the UI edits one route per instance
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
            info.name = row->getNameText();
            info.stateText = row->getStateText();
            info.shownAsMissing = row->isShownAsMissing();
            info.loadingIndicator = row->isLoadingIndicatorVisible();
            info.meterDb[0] = row->getMeterDb (0);
            info.meterDb[1] = row->getMeterDb (1);
            info.editorEnabled = row->isEditorEnabled();
            info.duplicateEnabled = row->isDuplicateEnabled();
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
        row->tick (graph.findInstance (row->getId()), router.getActivityCount (row->getId()), now);
}

//==============================================================================
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

    const auto contentHeight = rows.isEmpty() ? viewport.getMaximumVisibleHeight()
                                              : rows.size() * (kRowHeight + kRowGap) - kRowGap;
    rowList->setSize (viewport.getMaximumVisibleWidth(), juce::jmax (kRowHeight, contentHeight));
    rowList->resized();
    rowList->repaint();

    const auto numLoading = graph.getNumLoading();
    titleLabel.setText ("Instancias (" + juce::String ((int) instances.size()) + ")"
                            + (numLoading > 0 ? juce::String::fromUTF8 ("  —  cargando ") + juce::String (numLoading) + juce::String::fromUTF8 ("…")
                                              : juce::String()),
                        juce::dontSendNotification);
    updateKeyboardHeader();
}

void InstancePanel::setStatus (const juce::String& text, bool isError)
{
    if (onStatus)
        onStatus (text, isError);
}

void InstancePanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // Keyboard header strip.
    auto area = getLocalBounds();
    auto keyboardArea = area.removeFromBottom (kKeyboardHeaderHeight + (keyboardVisible ? kKeyboardHeight : 0));
    g.setColour (theme::bar);
    g.fillRect (keyboardArea.removeFromTop (kKeyboardHeaderHeight));
    g.setColour (theme::outline);
    g.drawHorizontalLine (getHeight() - (kKeyboardHeaderHeight + (keyboardVisible ? kKeyboardHeight : 0)), 0.0f, (float) getWidth());
}

void InstancePanel::resized()
{
    auto area = getLocalBounds();

    auto keyboardArea = area.removeFromBottom (kKeyboardHeaderHeight + (keyboardVisible ? kKeyboardHeight : 0));
    auto keyboardHeader = keyboardArea.removeFromTop (kKeyboardHeaderHeight).reduced (8, 3);
    keyboardToggle.setBounds (keyboardHeader.removeFromLeft (92));
    keyboardHeader.removeFromLeft (8);
    keyboardLabel.setBounds (keyboardHeader);
    keyboard.setBounds (keyboardArea);

    area.reduce (8, 0);
    auto header = area.removeFromTop (kHeaderHeight).reduced (0, 4);
    addButton.setBounds (header.removeFromRight (190));
    header.removeFromRight (8);
    titleLabel.setBounds (header);

    area.removeFromBottom (6);
    viewport.setBounds (area);

    updateRows();
}

} // namespace vstplayer
