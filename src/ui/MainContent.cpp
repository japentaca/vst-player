#include "ui/MainContent.h"

#include "engine/AudioEngine.h"
#include "engine/PluginManager.h"
#include "ui/InstancePanel.h"

namespace vstplayer
{

MainContent::MainContent (AudioEngine& e, PluginManager& p, InstanceGraph& instances, MidiRouter& router)
    : engine (e), plugins (p)
{
    titleLabel.setText (juce::String::fromUTF8 ("Proyecto: Sin título"), juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (18.0f));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    for (auto* label : { &audioLabel, &pluginsLabel })
    {
        label->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*label);
    }

    errorLabel.setColour (juce::Label::textColourId, juce::Colours::orangered);
    errorLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (errorLabel);

    toneButton.setButtonText (juce::String::fromUTF8 ("Tono de prueba 440 Hz"));
    toneButton.setClickingTogglesState (true);
    toneButton.setColour (juce::TextButton::buttonOnColourId, juce::Colours::darkorange);
    toneButton.onClick = [this] { engine.setTestToneEnabled (toneButton.getToggleState()); };
    addAndMakeVisible (toneButton);

    audioButton.setButtonText (juce::String::fromUTF8 ("Configuración de audio…"));
    audioButton.onClick = [this] { if (onShowAudioSettings) onShowAudioSettings(); };
    addAndMakeVisible (audioButton);

    pluginsButton.setButtonText (juce::String::fromUTF8 ("Plugins…"));
    pluginsButton.onClick = [this] { if (onShowPlugins) onShowPlugins(); };
    addAndMakeVisible (pluginsButton);

    instancePanel = std::make_unique<InstancePanel> (instances, router, plugins);
    addAndMakeVisible (*instancePanel);

    engine.getDeviceManager().addChangeListener (this);
    plugins.getKnownPluginList().addChangeListener (this);

    setSize (1024, 640);
    refresh();
}

MainContent::~MainContent()
{
    plugins.getKnownPluginList().removeChangeListener (this);
    engine.getDeviceManager().removeChangeListener (this);
}

void MainContent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainContent::resized()
{
    auto area = getLocalBounds().reduced (16);

    titleLabel.setBounds (area.removeFromTop (32));
    area.removeFromTop (8);
    audioLabel.setBounds (area.removeFromTop (24));
    pluginsLabel.setBounds (area.removeFromTop (24));
    errorLabel.setBounds (area.removeFromTop (errorLabel.getText().isEmpty() ? 0 : 24));
    area.removeFromTop (12);

    auto buttons = area.removeFromTop (32);
    toneButton.setBounds (buttons.removeFromLeft (200));
    buttons.removeFromLeft (8);
    audioButton.setBounds (buttons.removeFromLeft (200));
    buttons.removeFromLeft (8);
    pluginsButton.setBounds (buttons.removeFromLeft (140));

    area.removeFromTop (12);
    instancePanel->setBounds (area);
}

void MainContent::setProjectInfo (const juce::String& text)
{
    titleLabel.setText (text, juce::dontSendNotification);
}

void MainContent::setAudioError (const juce::String& message)
{
    errorLabel.setText (message.isEmpty() ? juce::String() : "Error de audio: " + message, juce::dontSendNotification);
    resized();
}

void MainContent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine.getDeviceManager() && engine.getDeviceManager().getCurrentAudioDevice() != nullptr)
        setAudioError ({});

    refresh();
}

void MainContent::refresh()
{
    audioLabel.setText (engine.describeDevice(), juce::dontSendNotification);

    auto& list = plugins.getKnownPluginList();
    int instruments = 0;

    for (auto& d : list.getTypes())
        instruments += d.isInstrument ? 1 : 0;

    pluginsLabel.setText ("Plugins VST3 conocidos: " + juce::String (list.getNumTypes())
                              + " (instrumentos: " + juce::String (instruments)
                              + ", desactivados: " + juce::String (list.getBlacklistedFiles().size()) + ")",
                          juce::dontSendNotification);

    toneButton.setToggleState (engine.isTestToneEnabled(), juce::dontSendNotification);
}

} // namespace vstplayer
