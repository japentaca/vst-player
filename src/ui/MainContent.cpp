#include "ui/MainContent.h"

#include "engine/AudioEngine.h"
#include "engine/GainProcessor.h"
#include "engine/InstanceGraph.h"
#include "engine/PluginManager.h"
#include "ui/InstancePanel.h"
#include "ui/Theme.h"

namespace vstplayer
{

namespace
{
constexpr int kTopLine1Height = 40;
constexpr int kTopLine2Height = 30;
constexpr int kStatusHeight = 26;
constexpr int kTimerHz = 30;
constexpr int kCpuEveryTicks = 8; // ~4 updates per second
} // namespace

MainContent::MainContent (AudioEngine& e, PluginManager& p, InstanceGraph& i, MidiRouter& router)
    : engine (e), plugins (p), instances (i)
{
    projectLabel.setFont (juce::FontOptions (19.0f, juce::Font::bold));
    projectLabel.setMinimumHorizontalScale (0.7f);
    projectLabel.setText (juce::String::fromUTF8 ("Sin título"), juce::dontSendNotification);
    addAndMakeVisible (projectLabel);

    const auto setUpButton = [this] (juce::TextButton& b, const char* text, const char* tooltip, std::function<void()>* action)
    {
        b.setButtonText (juce::String::fromUTF8 (text));
        b.setTooltip (juce::String::fromUTF8 (tooltip));
        b.onClick = [action] { if (*action) (*action)(); };
        addAndMakeVisible (b);
    };

    setUpButton (newButton, "Nuevo", "Proyecto nuevo (Ctrl+N)", &onNew);
    setUpButton (openButton, "Abrir…", "Abrir un proyecto (Ctrl+O)", &onOpen);
    setUpButton (recentButton, "Recientes ▾", "Proyectos abiertos hace poco", &onRecent);
    setUpButton (saveButton, "Guardar", "Guardar el proyecto (Ctrl+S)", &onSave);
    setUpButton (saveAsButton, "Guardar como…", "Guardar con otro nombre (Ctrl+Shift+S)", &onSaveAs);
    setUpButton (audioDeviceButton, "Audio", "Configuración de audio: dispositivo, sample rate, buffer", &onShowAudioSettings);
    setUpButton (midiPortsButton, "Puertos MIDI…", "Estado de los puertos MIDI y últimos mensajes", &onShowMidiPorts);
    setUpButton (pluginsButton, "Plugins…", "Lista de plugins VST3 y escaneo", &onShowPlugins);

    recordButton.onClick = [this] { if (onToggleRecording) onToggleRecording(); };
    addAndMakeVisible (recordButton);
    refreshRecording();

    panicButton.setButtonText (juce::String::fromUTF8 ("PÁNICO"));
    panicButton.setTooltip (juce::String::fromUTF8 ("All notes off + all sound off en todas las instancias (Esc)"));
    panicButton.setColour (juce::TextButton::buttonColourId, theme::panic);
    panicButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    panicButton.onClick = [this] { instancePanel->panic(); };
    addAndMakeVisible (panicButton);

    audioInfoLabel.setColour (juce::Label::textColourId, theme::textDim);
    audioInfoLabel.setMinimumHorizontalScale (0.85f);
    addAndMakeVisible (audioInfoLabel);

    cpuLabel.setJustificationType (juce::Justification::centred);
    cpuLabel.setTooltip ("Carga del callback de audio (AudioDeviceManager::getCpuUsage)");
    addAndMakeVisible (cpuLabel);

    addAndMakeVisible (masterMeter);

    instancePanel = std::make_unique<InstancePanel> (instances, router, plugins);
    instancePanel->onStatus = [this] (const juce::String& text, bool isError) { setStatus (text, isError); };
    instancePanel->onShowPlugins = [this] { if (onShowPlugins) onShowPlugins(); };
    addAndMakeVisible (*instancePanel);

    statusLabel.setFont (juce::FontOptions (13.5f));
    statusLabel.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (statusLabel);

    addChildComponent (progressBar);
    progressLabel.setFont (juce::FontOptions (13.5f));
    progressLabel.setColour (juce::Label::textColourId, theme::accent);
    progressLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (progressLabel);

    hintLabel.setFont (juce::FontOptions (12.5f));
    hintLabel.setColour (juce::Label::textColourId, theme::textDim);
    hintLabel.setJustificationType (juce::Justification::centredRight);
    hintLabel.setText (juce::String::fromUTF8 ("Esc pánico · Ctrl+R grabar · Ctrl+S guardar · Ctrl+O abrir"), juce::dontSendNotification);
    addAndMakeVisible (hintLabel);

    apiLabel.setFont (juce::FontOptions (12.5f));
    apiLabel.setColour (juce::Label::textColourId, theme::textDim);
    apiLabel.setJustificationType (juce::Justification::centredRight);
    apiLabel.setMinimumHorizontalScale (0.8f);
    apiLabel.setTooltip (juce::String::fromUTF8 ("API local para páginas web: HTTP POST /api y WebSocket /ws (ver docs/api.md). Clientes = WebSockets abiertos."));
    addAndMakeVisible (apiLabel);

    engine.getDeviceManager().addChangeListener (this);
    plugins.getKnownPluginList().addChangeListener (this);

    setSize (1024, 640);
    refreshAudio();
    setStatus (juce::String::fromUTF8 ("Listo. Plugins VST3 conocidos: ") + juce::String (plugins.getKnownPluginList().getNumTypes()) + ".");
    startTimerHz (kTimerHz);
}

MainContent::~MainContent()
{
    stopTimer();
    plugins.getKnownPluginList().removeChangeListener (this);
    engine.getDeviceManager().removeChangeListener (this);
}

void MainContent::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto area = getLocalBounds();
    auto top = area.removeFromTop (kTopLine1Height + kTopLine2Height + 6);
    g.setColour (theme::bar);
    g.fillRect (top);
    g.setColour (theme::outline);
    g.drawHorizontalLine (top.getBottom() - 1, 0.0f, (float) getWidth());

    auto status = area.removeFromBottom (kStatusHeight);
    g.setColour (theme::bar);
    g.fillRect (status);
    g.setColour (theme::outline);
    g.drawHorizontalLine (status.getY(), 0.0f, (float) getWidth());
}

void MainContent::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (kTopLine1Height + kTopLine2Height + 6).reduced (8, 3);

    // Line 1: project + file actions + panic.
    auto line1 = top.removeFromTop (kTopLine1Height).reduced (0, 4);
    panicButton.setBounds (line1.removeFromRight (120));
    line1.removeFromRight (8);
    recordButton.setBounds (line1.removeFromRight (100));
    line1.removeFromRight (14);
    saveAsButton.setBounds (line1.removeFromRight (122));
    line1.removeFromRight (4);
    saveButton.setBounds (line1.removeFromRight (78));
    line1.removeFromRight (4);
    recentButton.setBounds (line1.removeFromRight (100));
    line1.removeFromRight (4);
    openButton.setBounds (line1.removeFromRight (70));
    line1.removeFromRight (4);
    newButton.setBounds (line1.removeFromRight (64));
    line1.removeFromRight (10);
    projectLabel.setBounds (line1);

    // Line 2: audio device, rate / buffer, CPU, master meter, MIDI ports, plugins.
    auto line2 = top.removeFromTop (kTopLine2Height).reduced (0, 2);
    pluginsButton.setBounds (line2.removeFromRight (90));
    line2.removeFromRight (4);
    midiPortsButton.setBounds (line2.removeFromRight (120));
    line2.removeFromRight (12);
    masterMeter.setBounds (line2.removeFromRight (110).withSizeKeepingCentre (110, 14));
    line2.removeFromRight (8);
    cpuLabel.setBounds (line2.removeFromRight (96));
    line2.removeFromRight (6);
    audioLineArea = line2;
    layOutAudioLine();

    // Status bar.
    auto status = area.removeFromBottom (kStatusHeight).reduced (8, 3);
    apiLabel.setBounds (status.removeFromRight (juce::jmin (210, status.getWidth() / 5)));
    status.removeFromRight (8);
    hintLabel.setBounds (status.removeFromRight (juce::jmin (300, status.getWidth() / 3)));
    status.removeFromRight (8);
    progressBar.setBounds (status.removeFromRight (110).reduced (0, 2));
    status.removeFromRight (6);
    progressLabel.setBounds (status.removeFromRight (juce::jmin (230, status.getWidth() / 3)));
    status.removeFromRight (6);
    statusLabel.setBounds (status);

    instancePanel->setBounds (area);
}

void MainContent::setProjectInfo (const juce::String& displayName, const juce::String& path, const juce::String& progress)
{
    projectLabel.setText (displayName, juce::dontSendNotification);
    projectLabel.setTooltip (path.isNotEmpty() ? path : juce::String::fromUTF8 ("Proyecto sin guardar"));
    saveButton.setTooltip (path.isNotEmpty() ? "Guardar en " + path + " (Ctrl+S)" : juce::String ("Guardar el proyecto (Ctrl+S)"));

    if (progress.isNotEmpty())
        progressLabel.setText (progress, juce::dontSendNotification);

    refreshProgress();
}

void MainContent::setAudioError (const juce::String& message)
{
    audioError = message;
    refreshAudio();

    if (message.isNotEmpty())
        setStatus ("Error de audio: " + message, true);
}

void MainContent::setStatus (const juce::String& text, bool isError)
{
    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, isError ? theme::error : theme::text);
    statusLabel.setTooltip (text);
}

void MainContent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine.getDeviceManager() && engine.getDeviceManager().getCurrentAudioDevice() != nullptr)
        audioError.clear();

    refreshAudio();
}

void MainContent::refreshAudio()
{
    const auto open = engine.getDeviceManager().getCurrentAudioDevice() != nullptr;

    audioDeviceText = open ? engine.getDeviceName() : juce::String::fromUTF8 ("Sin dispositivo de audio");
    audioDeviceButton.setTooltip ((open ? engine.getDeviceTypeName() + ": " + engine.getDeviceName() + juce::newLine : juce::String())
                                  + juce::String::fromUTF8 ("Clic: configuración de audio (dispositivo, sample rate, buffer)"));
    audioDeviceButton.setColour (juce::TextButton::buttonColourId,
                                 open && audioError.isEmpty() ? juce::Colour (0xff353a43) : theme::error.darker (0.6f));

    if (open)
    {
        const auto sampleRate = engine.getSampleRate();
        const auto latencyMs = sampleRate > 0.0 ? 1000.0 * engine.getOutputLatencySamples() / sampleRate : 0.0;
        audioInfoLabel.setText (juce::String (sampleRate, 0) + juce::String::fromUTF8 (" Hz · buffer ") + juce::String (engine.getBufferSize())
                                    + juce::String::fromUTF8 (" · ") + juce::String (latencyMs, 1) + " ms",
                                juce::dontSendNotification);
        audioInfoLabel.setTooltip (engine.getDeviceTypeName() + juce::String::fromUTF8 (": sample rate, buffer y latencia de salida"));
    }
    else
    {
        audioInfoLabel.setText (audioError.isNotEmpty() ? audioError : juce::String::fromUTF8 ("Abrí la configuración de audio"),
                                juce::dontSendNotification);
    }

    layOutAudioLine();
}

void MainContent::layOutAudioLine()
{
    // Device button as wide as its text (up to what leaves room for the rate / buffer line);
    // a name that is still too long is shortened with "…" (the full name is in the tooltip).
    auto line = audioLineArea;
    const auto font = getLookAndFeel().getTextButtonFont (audioDeviceButton, line.getHeight());
    const auto padding = 2 * juce::jmin (juce::roundToInt (font.getHeight() * 0.6f), 2 + line.getHeight() / 4) + 6;
    const auto infoWidth = juce::GlyphArrangement::getStringWidthInt (audioInfoLabel.getFont(), audioInfoLabel.getText()) + 12;
    const auto maxButton = juce::jmax (120, line.getWidth() - juce::jmin (infoWidth, line.getWidth() / 2) - 8);
    const auto wanted = juce::GlyphArrangement::getStringWidthInt (font, audioDeviceText) + padding;
    const auto buttonWidth = juce::jmin (wanted, maxButton);

    auto text = audioDeviceText;

    while (text.length() > 4 && juce::GlyphArrangement::getStringWidth (font, text) > (float) (buttonWidth - padding))
        text = text.dropLastCharacters (2).trimEnd() + juce::String::fromUTF8 ("…");

    audioDeviceButton.setButtonText (text);
    audioDeviceButton.setBounds (line.removeFromLeft (buttonWidth));
    line.removeFromLeft (8);
    audioInfoLabel.setBounds (line);
}

void MainContent::refreshProgress()
{
    const auto numLoading = instances.getNumLoading();
    const auto scanning = isScanning && isScanning();
    const auto busy = numLoading > 0 || scanning;

    if (busy)
    {
        juce::String text;

        if (scanning)
            text = juce::String::fromUTF8 ("Escaneando plugins…");
        else
            text = juce::String::fromUTF8 ("Cargando ") + juce::String (numLoading)
                   + (numLoading == 1 ? juce::String (" plugin") : juce::String (" plugins")) + juce::String::fromUTF8 ("…");

        progressLabel.setText (text, juce::dontSendNotification);
    }
    else
    {
        progressLabel.setText ({}, juce::dontSendNotification);
    }

    progressBar.setVisible (busy);
}

void MainContent::refreshRecording()
{
    const auto info = engine.getRecorder().getInfo();
    juce::String text, tooltip;

    if (info.active)
    {
        const auto total = (int) info.seconds;
        const auto h = total / 3600, m = (total / 60) % 60, sec = total % 60;
        text = juce::String::fromUTF8 ("■ ") + (h > 0 ? juce::String (h) + ":" + juce::String (m).paddedLeft ('0', 2) : juce::String (m))
               + ":" + juce::String (sec).paddedLeft ('0', 2);
        tooltip = juce::String::fromUTF8 ("Grabando (") + info.format.getLabel() + juce::String::fromUTF8 (") — clic o Ctrl+R para detener")
                  + juce::newLine + (info.mp3File != juce::File() ? info.mp3File : info.wavFile).getFullPathName();

        if (info.wavFile != juce::File() && info.mp3File != juce::File())
            tooltip << juce::newLine << info.wavFile.getFullPathName();

        if (info.peak > 1.0f)
            tooltip << juce::newLine << juce::String::fromUTF8 ("¡La mezcla pasó de 0 dBFS: el MP3 sale saturado!");
    }
    else
    {
        text = juce::String::fromUTF8 ("● Grabar");
        tooltip = juce::String::fromUTF8 ("Grabar lo que suena a un archivo (Ctrl+R). Formato y carpeta: Audio > Opciones de grabación…");
    }

    if (recordButton.getButtonText() != text)
        recordButton.setButtonText (text);

    recordButton.setTooltip (tooltip);
    const auto clipped = info.active && info.peak > 1.0f;
    recordButton.setColour (juce::TextButton::buttonColourId, info.active ? (clipped ? theme::warning.darker (0.3f) : theme::record)
                                                                          : juce::Colour (0xff353a43));
    recordButton.setColour (juce::TextButton::textColourOffId, info.active ? juce::Colours::white : theme::record.brighter (0.2f));
}

void MainContent::timerCallback()
{
    if (auto* mixer = engine.getMixer())
        masterMeter.pushPeaks (mixer->getAndResetMeterPeak (0), mixer->getAndResetMeterPeak (1));

    if (tickCount % 6 == 0)
        refreshRecording();

    if (++tickCount % kCpuEveryTicks != 0)
        return;

    const auto cpu = 100.0 * engine.getCpuUsage();
    cpuLabel.setText ("CPU " + juce::String (cpu, 1) + " %", juce::dontSendNotification);
    cpuLabel.setColour (juce::Label::textColourId, cpu > 80.0 ? theme::error : cpu > 50.0 ? theme::warning : theme::text);

    refreshProgress();

    if (apiStatusText)
    {
        apiLabel.setText (apiStatusText(), juce::dontSendNotification);
        apiLabel.setColour (juce::Label::textColourId, apiHasError && apiHasError() ? theme::error : theme::textDim);
    }
}

} // namespace vstplayer
