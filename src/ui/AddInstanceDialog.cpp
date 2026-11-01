#include "ui/AddInstanceDialog.h"

#include "ui/Theme.h"

namespace vstplayer
{

namespace
{
struct Item
{
    bool isHeader = false;
    juce::String manufacturer;
    juce::PluginDescription description;
};

juce::String manufacturerOf (const juce::PluginDescription& d)
{
    return d.manufacturerName.trim().isNotEmpty() ? d.manufacturerName.trim() : juce::String ("(sin fabricante)");
}
} // namespace

//==============================================================================
class AddInstanceDialog::Content final : public juce::Component,
                                         private juce::ListBoxModel,
                                         private juce::ChangeListener,
                                         private juce::TextEditor::Listener
{
public:
    Content (AddInstanceDialog& w, juce::KnownPluginList& l)
        : window (w), list (l)
    {
        searchBox.setTextToShowWhenEmpty (juce::String::fromUTF8 ("Buscar por nombre, fabricante o categoría…"), theme::textDim);
        searchBox.addListener (this);
        searchBox.onTextChange = [this] { rebuild(); };
        addAndMakeVisible (searchBox);

        instrumentsOnly.setButtonText (juce::String::fromUTF8 ("Sólo instrumentos"));
        instrumentsOnly.setToggleState (true, juce::dontSendNotification);
        instrumentsOnly.onClick = [this] { rebuild(); };
        addAndMakeVisible (instrumentsOnly);

        listBox.setModel (this);
        listBox.setRowHeight (26);
        listBox.setOutlineThickness (1);
        addAndMakeVisible (listBox);

        countLabel.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (countLabel);

        addButton.setButtonText ("Agregar");
        addButton.setColour (juce::TextButton::buttonColourId, theme::accent.darker (0.35f));
        addButton.onClick = [this] { addSelected(); };
        addAndMakeVisible (addButton);

        cancelButton.setButtonText ("Cancelar");
        cancelButton.onClick = [this] { window.closeButtonPressed(); };
        addAndMakeVisible (cancelButton);

        pluginsButton.setButtonText (juce::String::fromUTF8 ("Plugins…"));
        pluginsButton.setTooltip (juce::String::fromUTF8 ("Lista de plugins y escaneo"));
        pluginsButton.onClick = [this] { if (window.onShowPlugins) window.onShowPlugins(); };
        addAndMakeVisible (pluginsButton);

        list.addChangeListener (this);
        rebuild();
        setSize (540, 480);
    }

    ~Content() override
    {
        list.removeChangeListener (this);
        searchBox.removeListener (this);
        listBox.setModel (nullptr);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        auto top = area.removeFromTop (28);
        instrumentsOnly.setBounds (top.removeFromRight (150));
        top.removeFromRight (8);
        searchBox.setBounds (top);
        area.removeFromTop (8);

        auto bottom = area.removeFromBottom (30);
        cancelButton.setBounds (bottom.removeFromRight (96));
        bottom.removeFromRight (8);
        addButton.setBounds (bottom.removeFromRight (96));
        bottom.removeFromRight (8);
        pluginsButton.setBounds (bottom.removeFromLeft (96));
        bottom.removeFromLeft (8);
        countLabel.setBounds (bottom);
        area.removeFromBottom (8);
        listBox.setBounds (area);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::bar);
    }

    void focusSearch()
    {
        if (isShowing())
            searchBox.grabKeyboardFocus();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            window.closeButtonPressed();
            return true;
        }

        if (key == juce::KeyPress::returnKey)
        {
            enterPressed();
            return true;
        }

        if (key == juce::KeyPress::downKey && searchBox.hasKeyboardFocus (false))
        {
            if (selectedPluginRow() < 0)
                selectPlugin (0);

            listBox.grabKeyboardFocus();
            return true;
        }

        return false;
    }

    //==============================================================================
    void setSearchText (const juce::String& text)
    {
        searchBox.setText (text, false);
        rebuild();
    }

    void setInstrumentsOnly (bool b)
    {
        instrumentsOnly.setToggleState (b, juce::dontSendNotification);
        rebuild();
    }

    bool isInstrumentsOnly() const { return instrumentsOnly.getToggleState(); }

    juce::Array<juce::PluginDescription> getListedPlugins() const
    {
        juce::Array<juce::PluginDescription> result;

        for (auto& item : items)
            if (! item.isHeader)
                result.add (item.description);

        return result;
    }

    juce::StringArray getListedManufacturers() const
    {
        juce::StringArray result;

        for (auto& item : items)
            if (item.isHeader)
                result.add (item.manufacturer);

        return result;
    }

    bool selectPlugin (int pluginIndex)
    {
        int n = 0;

        for (int row = 0; row < (int) items.size(); ++row)
        {
            if (items[(size_t) row].isHeader)
                continue;

            if (n++ == pluginIndex)
            {
                listBox.selectRow (row);
                return true;
            }
        }

        return false;
    }

    bool addSelected()
    {
        const auto row = selectedPluginRow();

        if (row < 0)
            return false;

        const auto description = items[(size_t) row].description;
        juce::Component::SafePointer<AddInstanceDialog> safeWindow (&window);

        if (window.onAdd)
            window.onAdd (description);

        if (safeWindow != nullptr)
            safeWindow->closeButtonPressed();

        return true;
    }

private:
    int selectedPluginRow() const
    {
        const auto row = listBox.getSelectedRow();
        return juce::isPositiveAndBelow (row, (int) items.size()) && ! items[(size_t) row].isHeader ? row : -1;
    }

    void enterPressed()
    {
        if (selectedPluginRow() < 0)
            selectPlugin (0);

        addSelected();
    }

    void rebuild()
    {
        const auto selectedRow = selectedPluginRow();
        const auto previous = selectedRow >= 0 ? items[(size_t) selectedRow].description.createIdentifierString() : juce::String();
        const auto words = juce::StringArray::fromTokens (searchBox.getText().trim(), true);
        const auto onlyInstruments = instrumentsOnly.getToggleState();

        auto types = list.getTypes();
        std::stable_sort (types.begin(), types.end(), [] (const auto& a, const auto& b)
        {
            const auto m = manufacturerOf (a).compareIgnoreCase (manufacturerOf (b));
            return m != 0 ? m < 0 : a.name.compareIgnoreCase (b.name) < 0;
        });

        items.clear();
        int numPlugins = 0;

        for (auto& d : types)
        {
            if (onlyInstruments && ! d.isInstrument)
                continue;

            const auto haystack = d.name + " " + d.manufacturerName + " " + d.category + " " + d.descriptiveName;
            auto matches = true;

            for (auto& w : words)
                matches = matches && haystack.containsIgnoreCase (w);

            if (! matches)
                continue;

            if (items.empty() || manufacturerOf (d) != items.back().manufacturer)
                items.push_back ({ true, manufacturerOf (d), {} });

            items.push_back ({ false, manufacturerOf (d), d });
            ++numPlugins;
        }

        listBox.updateContent();
        listBox.repaint();

        int toSelect = -1;

        for (int row = 0; row < (int) items.size(); ++row)
            if (! items[(size_t) row].isHeader && items[(size_t) row].description.createIdentifierString() == previous)
                toSelect = row;

        if (toSelect >= 0)
            listBox.selectRow (toSelect);
        else
            listBox.deselectAllRows();

        if (list.getNumTypes() == 0)
            countLabel.setText (juce::String::fromUTF8 ("No hay plugins: usá \"Plugins…\" para escanear."), juce::dontSendNotification);
        else
            countLabel.setText (juce::String (numPlugins) + " de " + juce::String (list.getNumTypes()) + " plugins",
                                juce::dontSendNotification);

        addButton.setEnabled (selectedPluginRow() >= 0);
    }

    // ListBoxModel
    int getNumRows() override { return (int) items.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) items.size()))
            return;

        const auto& item = items[(size_t) row];

        if (item.isHeader)
        {
            g.fillAll (theme::bar);
            g.setColour (theme::accent);
            g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
            g.drawText (item.manufacturer, 8, 0, width - 16, height, juce::Justification::centredLeft);
            return;
        }

        if (selected)
            g.fillAll (theme::accent.darker (0.45f));

        const auto& d = item.description;
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (d.name, 24, 0, width - 200, height, juce::Justification::centredLeft, true);

        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (12.5f));
        const auto info = (d.isInstrument ? juce::String ("Instrumento") : juce::String ("Efecto"))
                          + (d.version.isNotEmpty() ? "  " + d.version : juce::String());
        g.drawText (info, width - 170, 0, 162, height, juce::Justification::centredRight, true);
    }

    void selectedRowsChanged (int lastRowSelected) override
    {
        // Group headers are not selectable: move to the plugin below.
        if (juce::isPositiveAndBelow (lastRowSelected, (int) items.size()) && items[(size_t) lastRowSelected].isHeader)
        {
            if (lastRowSelected + 1 < (int) items.size())
                listBox.selectRow (lastRowSelected + 1);
            else
                listBox.deselectAllRows();
        }

        addButton.setEnabled (selectedPluginRow() >= 0);
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        if (juce::isPositiveAndBelow (row, (int) items.size()) && ! items[(size_t) row].isHeader)
        {
            listBox.selectRow (row);
            addSelected();
        }
    }

    void returnKeyPressed (int) override { addSelected(); }

    juce::String getTooltipForRow (int row) override
    {
        return juce::isPositiveAndBelow (row, (int) items.size()) && ! items[(size_t) row].isHeader
                   ? items[(size_t) row].description.fileOrIdentifier
                   : juce::String();
    }

    // The KnownPluginList changed (a scan finished, a plugin was removed...).
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }

    // TextEditor::Listener (the search box consumes Return / Esc itself).
    void textEditorReturnKeyPressed (juce::TextEditor&) override { enterPressed(); }
    void textEditorEscapeKeyPressed (juce::TextEditor&) override { window.closeButtonPressed(); }

    AddInstanceDialog& window;
    juce::KnownPluginList& list;
    std::vector<Item> items;

    juce::TextEditor searchBox;
    juce::ToggleButton instrumentsOnly;
    juce::ListBox listBox { "plugins" };
    juce::Label countLabel;
    juce::TextButton addButton, cancelButton, pluginsButton;
};

//==============================================================================
AddInstanceDialog::AddInstanceDialog (juce::KnownPluginList& list, juce::Component* centreAround)
    : DocumentWindow ("Agregar instancia", theme::bar, DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (*this, list), true);
    setResizable (true, false);
    setResizeLimits (420, 320, 2000, 2000);

    if (centreAround != nullptr)
        centreAroundComponent (centreAround, getWidth(), getHeight());
    else
        centreWithSize (getWidth(), getHeight());

    setVisible (true);
    toFront (true);
    getContent().focusSearch();
}

AddInstanceDialog::~AddInstanceDialog() = default;

void AddInstanceDialog::closeButtonPressed()
{
    setVisible (false);

    if (onClose)
        onClose();
}

bool AddInstanceDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        closeButtonPressed();
        return true;
    }

    return DocumentWindow::keyPressed (key);
}

AddInstanceDialog::Content& AddInstanceDialog::getContent() const
{
    return *dynamic_cast<Content*> (getContentComponent());
}

void AddInstanceDialog::setSearchText (const juce::String& text) { getContent().setSearchText (text); }
void AddInstanceDialog::setInstrumentsOnly (bool b) { getContent().setInstrumentsOnly (b); }
bool AddInstanceDialog::isInstrumentsOnly() const { return getContent().isInstrumentsOnly(); }
juce::Array<juce::PluginDescription> AddInstanceDialog::getListedPlugins() const { return getContent().getListedPlugins(); }
juce::StringArray AddInstanceDialog::getListedManufacturers() const { return getContent().getListedManufacturers(); }
bool AddInstanceDialog::selectPlugin (int index) { return getContent().selectPlugin (index); }
bool AddInstanceDialog::addSelected() { return getContent().addSelected(); }

} // namespace vstplayer
