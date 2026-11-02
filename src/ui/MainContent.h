#pragma once

#include "ui/LevelMeter.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>

namespace vstplayer
{

class AudioEngine;
class InstanceGraph;
class InstancePanel;
class MidiRouter;
class PluginManager;

// Content of the main window (stage 6):
//   - Top bar, line 1: project name ("*" when modified; the path in the tooltip), Nuevo,
//     Abrir…, Recientes, Guardar, Guardar como…, "● Grabar" (red with the elapsed time while
//     recording; Ctrl+R) and the Pánico button.
//     Line 2: audio device (button: opens the audio settings), sample rate / buffer / latency,
//     audio CPU %, master output meter, "Puertos MIDI…", "Plugins…".
//   - Centre: InstancePanel (instance rows, foldable virtual keyboard).
//   - Status bar: last message (errors in red), progress indicator while plugins load or a
//     scan runs, shortcut hint.
class MainContent final : public juce::Component,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    MainContent (AudioEngine& engine, PluginManager& plugins, InstanceGraph& instances, MidiRouter& router);
    ~MainContent() override;

    // Project line: display name (with " *" if modified), file path (tooltip) and loading progress.
    void setProjectInfo (const juce::String& displayName, const juce::String& path, const juce::String& progress);
    juce::String getProjectInfo() const { return projectLabel.getText(); }

    // Last audio device error (empty clears it). Cleared automatically when the device changes.
    void setAudioError (const juce::String& message);

    // Status bar message (errors in red). Stays until the next one.
    void setStatus (const juce::String& text, bool isError = false);
    juce::String getStatusText() const { return statusLabel.getText(); }

    InstancePanel& getInstancePanel() noexcept { return *instancePanel; }

    // Buttons (MainWindow connects them to the same actions as the menu).
    std::function<void()> onNew, onOpen, onRecent, onSave, onSaveAs;
    std::function<void()> onShowAudioSettings, onShowMidiPorts, onShowPlugins;
    std::function<void()> onToggleRecording;
    // True while a plugin scan runs (status bar progress).
    std::function<bool()> isScanning;
    // Local API indicator in the status bar ("API: 127.0.0.1:9471, 1 cliente").
    std::function<juce::String()> apiStatusText;
    std::function<bool()> apiHasError;
    juce::String getApiText() const { return apiLabel.getText(); }

    // What the top bar / status bar show (tests).
    juce::String getAudioText() const { return audioInfoLabel.getText(); }
    juce::String getCpuText() const { return cpuLabel.getText(); }
    bool isProgressVisible() const { return progressBar.isVisible(); }
    juce::String getProgressText() const { return progressLabel.getText(); }
    juce::Button& getRecentButton() noexcept { return recentButton; }
    juce::TextButton& getRecordButton() noexcept { return recordButton; }
    // Updates the record button from the recorder (also done by the timer).
    void refreshRecording();
    float getMasterMeterDb (int channel) const { return masterMeter.getLevelDb (channel); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshAudio();
    void layOutAudioLine();
    void refreshProgress();

    AudioEngine& engine;
    PluginManager& plugins;
    InstanceGraph& instances;

    // Top bar.
    juce::Label projectLabel;
    juce::TextButton newButton, openButton, recentButton, saveButton, saveAsButton;
    juce::TextButton recordButton;
    juce::TextButton panicButton;
    juce::TextButton audioDeviceButton;
    juce::Label audioInfoLabel;
    juce::Rectangle<int> audioLineArea;
    juce::String audioDeviceText;
    juce::Label cpuLabel;
    LevelMeter masterMeter;
    juce::TextButton midiPortsButton, pluginsButton;

    std::unique_ptr<InstancePanel> instancePanel;

    // Status bar.
    juce::Label statusLabel;
    double progressValue = -1.0; // indeterminate
    juce::ProgressBar progressBar { progressValue };
    juce::Label progressLabel;
    juce::Label hintLabel;
    juce::Label apiLabel;

    juce::String audioError;
    int tickCount = 0;

    juce::TooltipWindow tooltipWindow { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainContent)
};

} // namespace vstplayer
