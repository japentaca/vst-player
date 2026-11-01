#include "ui/PluginEditorWindow.h"

namespace vstplayer
{

PluginEditorWindow::PluginEditorWindow (juce::AudioProcessor& p, const juce::String& title)
    : DocumentWindow (title, juce::Colours::darkgrey, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
      processor (p)
{
    setUsingNativeTitleBar (true);

    juce::AudioProcessorEditor* editor = processor.hasEditor() ? processor.createEditorAndMakeActive() : nullptr;

    if (editor == nullptr)
    {
        genericEditor = true;
        editor = new juce::GenericAudioProcessorEditor (processor);
        editor->setSize (juce::jmax (400, editor->getWidth()), juce::jlimit (200, 600, editor->getHeight()));
    }

    setContentOwned (editor, true);
    setResizable (editor->isResizable() || genericEditor, false);
}

PluginEditorWindow::~PluginEditorWindow()
{
    // Deletes the editor (AudioProcessorEditor's destructor tells the processor) while the
    // processor is still alive.
    clearContentComponent();
}

void PluginEditorWindow::closeButtonPressed()
{
    if (onCloseRequested)
        onCloseRequested(); // may delete this
}

juce::AudioProcessorEditor* PluginEditorWindow::getEditor() const
{
    return dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent());
}

} // namespace vstplayer
