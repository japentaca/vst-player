#include "ui/MidiPortsWindow.h"

#include "engine/MidiRouter.h"

namespace vstplayer
{

namespace
{
juce::String formatTime (double seconds)
{
    // Timestamps use the millisecond counter (time since boot): show it as wall-clock time.
    const auto ageMs = juce::Time::getMillisecondCounterHiRes() - seconds * 1000.0;
    const auto when = juce::Time::getCurrentTime() - juce::RelativeTime::milliseconds ((juce::int64) ageMs);
    return when.formatted ("%H:%M:%S") + juce::String::formatted (".%03d", when.getMilliseconds());
}

juce::String describeMessage (const juce::MidiMessage& m)
{
    if (m.isSysEx())
        return "SysEx (" + juce::String (m.getSysExDataSize()) + " bytes)";

    if (m.isMidiClock())
        return "Clock";

    if (m.isActiveSense())
        return "Active sensing";

    juce::String hex;

    for (int i = 0; i < juce::jmin (3, m.getRawDataSize()); ++i)
        hex << juce::String::toHexString ((int) m.getRawData()[i]).paddedLeft ('0', 2).toUpperCase() << ' ';

    return hex.paddedRight (' ', 10) + m.getDescription();
}
} // namespace

//==============================================================================
class MidiPortsWindow::Content final : public juce::Component,
                                       private juce::Timer,
                                       private juce::ChangeListener
{
public:
    explicit Content (MidiRouter& r)
        : router (r)
    {
        monitorToggle.setButtonText (juce::String::fromUTF8 ("Escuchar todos los puertos mientras esta ventana esté abierta"));
        monitorToggle.setToggleState (router.isMonitoringAllPorts(), juce::dontSendNotification);
        monitorToggle.onClick = [this] { router.setMonitorAllPorts (monitorToggle.getToggleState()); };
        addAndMakeVisible (monitorToggle);

        refreshButton.setButtonText ("Actualizar lista");
        refreshButton.onClick = [this] { router.refreshDevices(); };
        addAndMakeVisible (refreshButton);

        clearButton.setButtonText ("Limpiar log");
        clearButton.onClick = [this]
        {
            clearedUpTo = router.getLogSerial();
            lastSerial = 0;
            updateLog();
        };
        addAndMakeVisible (clearButton);

        for (auto* label : { &portsLabel, &logLabel })
        {
            label->setColour (juce::Label::textColourId, juce::Colours::lightgrey);
            addAndMakeVisible (*label);
        }

        portsLabel.setText ("Puertos de entrada", juce::dontSendNotification);
        logLabel.setText (juce::String::fromUTF8 ("Últimos mensajes recibidos (los más nuevos abajo)"), juce::dontSendNotification);

        for (auto* editor : { &portsText, &logText })
        {
            editor->setMultiLine (true);
            editor->setReadOnly (true);
            editor->setScrollbarsShown (true);
            editor->setCaretVisible (false);
            editor->setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
            addAndMakeVisible (*editor);
        }

        router.addChangeListener (this);
        updatePorts();
        updateLog();
        startTimerHz (10);
        setSize (780, 520);
    }

    ~Content() override
    {
        stopTimer();
        router.removeChangeListener (this);
        router.setMonitorAllPorts (false);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        auto top = area.removeFromTop (26);
        refreshButton.setBounds (top.removeFromRight (130));
        top.removeFromRight (8);
        monitorToggle.setBounds (top);

        area.removeFromTop (6);
        portsLabel.setBounds (area.removeFromTop (20));
        portsText.setBounds (area.removeFromTop (130));
        area.removeFromTop (8);

        auto logHeader = area.removeFromTop (24);
        clearButton.setBounds (logHeader.removeFromRight (110));
        logLabel.setBounds (logHeader);
        logText.setBounds (area);
    }

    void syncToggle() { monitorToggle.setToggleState (router.isMonitoringAllPorts(), juce::dontSendNotification); }

    juce::TextEditor portsText, logText;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        syncToggle();
        updatePorts();
    }

    void timerCallback() override
    {
        updatePorts();
        updateLog();
    }

    void updatePorts()
    {
        juce::String text;

        for (auto& port : router.getPortStatus())
        {
            juce::String state = port.open ? "abierto" : (port.available ? "cerrado" : "DESCONECTADO");

            if (port.error.isNotEmpty())
                state = "ERROR: " + port.error;

            text << port.name.paddedRight (' ', 28) << "  " << state.paddedRight (' ', 14)
                 << "  rutas: " << port.numRoutes << "  mensajes: " << juce::String (port.messagesReceived) << "\n";
        }

        if (text.isEmpty())
            text = juce::String::fromUTF8 ("No hay puertos MIDI de entrada (¿loopMIDI o un teclado MIDI?). No hacen falta para "
                                           "tocar desde carrilion o la API: rutas \"Sin puerto\".\n");

        // Routes with no port: no port reaches them; carrilion / the API (midi without
        // instanceId) / --render use them. Shown so the route counts add up.
        int portless = 0;

        for (auto& route : router.getRoutes())
            if (! route.hasPort())
                ++portless;

        if (portless > 0)
            text << juce::String::fromUTF8 ("(sin puerto: sólo API / carrilion)").paddedRight (' ', 28) << "  "
                 << juce::String().paddedRight (' ', 14) << "  rutas: " << portless << "\n";

        if (text != portsText.getText())
            portsText.setText (text, false);
    }

    void updateLog()
    {
        const auto serial = router.getLogSerial();

        if (serial == lastSerial)
            return;

        lastSerial = serial;
        juce::String text;

        for (auto& entry : router.getRecentMessages())
        {
            if (entry.serial <= clearedUpTo)
                continue;

            juce::String result;

            if (entry.dropped)
                result = "descartado (sistema)";
            else if (entry.numTargets == 0)
                result = "sin ruta";
            else
                result = juce::String::fromUTF8 ("→ ") + juce::String (entry.numTargets) + " instancia(s)";

            text << formatTime (entry.time) << "  " << entry.device.paddedRight (' ', 18) << "  "
                 << describeMessage (entry.message).paddedRight (' ', 46) << "  " << result << "\n";
        }

        logText.setText (text, false);
        logText.moveCaretToEnd();
    }

    MidiRouter& router;
    juce::ToggleButton monitorToggle;
    juce::TextButton refreshButton, clearButton;
    juce::Label portsLabel, logLabel;
    juce::uint64 lastSerial = 0, clearedUpTo = 0;
};

//==============================================================================
MidiPortsWindow::MidiPortsWindow (MidiRouter& r)
    : DocumentWindow ("Puertos MIDI",
                      juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::closeButton),
      router (r)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (router), true);
    setResizable (true, false);
    setResizeLimits (500, 360, 4000, 3000);
}

MidiPortsWindow::~MidiPortsWindow()
{
    clearContentComponent();
}

void MidiPortsWindow::closeButtonPressed()
{
    setVisible (false);
    router.setMonitorAllPorts (false);
}

juce::String MidiPortsWindow::getPortsText() const
{
    if (auto* content = dynamic_cast<Content*> (getContentComponent()))
        return content->portsText.getText();

    return {};
}

juce::String MidiPortsWindow::getLogText() const
{
    if (auto* content = dynamic_cast<Content*> (getContentComponent()))
        return content->logText.getText();

    return {};
}

} // namespace vstplayer
