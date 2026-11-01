#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <vector>

namespace vstplayer
{

// "Agregar instancia" window (stage 6): search box (name, manufacturer or category), "Sólo
// instrumentos" filter and the KnownPluginList grouped by manufacturer. Double click, Enter or
// "Agregar" adds the selected plugin and closes the window; Esc or "Cancelar" closes it.
// "Plugins…" opens the plugin list (scan). The list follows changes of the KnownPluginList
// (e.g. a scan finishing while the window is open). Not modal.
class AddInstanceDialog final : public juce::DocumentWindow
{
public:
    AddInstanceDialog (juce::KnownPluginList& list, juce::Component* centreAround);
    ~AddInstanceDialog() override;

    std::function<void (const juce::PluginDescription&)> onAdd; // the window closes itself after
    std::function<void()> onClose;                              // the owner deletes the window (async)
    std::function<void()> onShowPlugins;

    void closeButtonPressed() override;
    bool keyPressed (const juce::KeyPress& key) override; // Esc closes, wherever the focus is

    // Same paths as the controls (tests).
    void setSearchText (const juce::String& text);
    void setInstrumentsOnly (bool shouldFilter);
    bool isInstrumentsOnly() const;
    // Plugins currently listed (in display order) and the manufacturer groups shown.
    juce::Array<juce::PluginDescription> getListedPlugins() const;
    juce::StringArray getListedManufacturers() const;
    // Selects the n-th listed plugin (not counting group headers). False if out of range.
    bool selectPlugin (int index);
    // Presses "Agregar" (false if nothing is selected).
    bool addSelected();

private:
    class Content;
    Content& getContent() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AddInstanceDialog)
};

} // namespace vstplayer
