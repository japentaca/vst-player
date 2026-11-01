#include "ui/PluginListWindow.h"

#include "app/AppSettings.h"
#include "engine/PluginManager.h"

namespace vstplayer
{

namespace
{
// One background thread: keeps the UI responsive (each probe is a child process anyway).
// More than one is unsafe in JUCE 8.0.15: PluginListComponent's Scanner marks the scan as
// finished as soon as *one* thread takes the last file, and then cancels the jobs of the
// other threads, so a plugin still being probed elsewhere is silently dropped.
constexpr int kScanThreads = 1;

// Table model with the columns the plan asks for. Row order matches the one
// PluginListComponent uses internally (types first, then blacklisted files), so its
// remove / context-menu helpers keep working.
class PluginTableModel final : public juce::TableListBoxModel
{
public:
    enum Column
    {
        nameCol = 1,
        manufacturerCol,
        versionCol,
        instrumentCol,
        categoryCol,
        pathCol
    };

    PluginTableModel (juce::PluginListComponent& o, juce::KnownPluginList& l) : owner (o), list (l) {}

    static void setUpColumns (juce::TableHeaderComponent& header)
    {
        const auto flags = juce::TableHeaderComponent::defaultFlags;
        header.removeAllColumns();
        header.addColumn ("Nombre", nameCol, 180, 80, 500, flags | juce::TableHeaderComponent::sortedForwards);
        header.addColumn ("Fabricante", manufacturerCol, 150, 80, 300, flags);
        header.addColumn (juce::String::fromUTF8 ("Versión"), versionCol, 80, 50, 150, flags & ~juce::TableHeaderComponent::sortable);
        header.addColumn ("Instrumento", instrumentCol, 85, 60, 120, flags & ~juce::TableHeaderComponent::sortable);
        header.addColumn (juce::String::fromUTF8 ("Categoría"), categoryCol, 110, 60, 250, flags);
        header.addColumn ("Ruta", pathCol, 380, 100, 1200, flags);
    }

    int getNumRows() override
    {
        return list.getNumTypes() + list.getBlacklistedFiles().size();
    }

    void paintRowBackground (juce::Graphics& g, int, int, int, bool rowIsSelected) override
    {
        const auto base = owner.findColour (juce::ListBox::backgroundColourId);
        g.fillAll (rowIsSelected ? base.interpolatedWith (owner.findColour (juce::ListBox::textColourId), 0.4f) : base);
    }

    void paintCell (juce::Graphics& g, int row, int columnId, int width, int height, bool) override
    {
        juce::String text;
        const auto numTypes = list.getNumTypes();
        const bool isBlacklisted = row >= numTypes;

        if (isBlacklisted)
        {
            const auto file = list.getBlacklistedFiles()[row - numTypes];

            if (columnId == nameCol)
                text = juce::File::createFileWithoutCheckingPath (file).getFileName();
            else if (columnId == categoryCol)
                text = "Desactivado (falló al escanear)";
            else if (columnId == pathCol)
                text = file;
        }
        else
        {
            const auto types = list.getTypes();

            if (! juce::isPositiveAndBelow (row, types.size()))
                return;

            const auto& d = types.getReference (row);

            switch (columnId)
            {
                case nameCol:         text = d.name; break;
                case manufacturerCol: text = d.manufacturerName; break;
                case versionCol:      text = d.version; break;
                case instrumentCol:   text = juce::String::fromUTF8 (d.isInstrument ? "Sí" : "No"); break;
                case categoryCol:     text = d.category.isNotEmpty() ? d.category : "-"; break;
                case pathCol:         text = d.fileOrIdentifier; break;
                default: break;
            }
        }

        if (text.isEmpty())
            return;

        g.setColour (isBlacklisted ? juce::Colours::red : owner.findColour (juce::ListBox::textColourId));
        g.setFont (juce::FontOptions ((float) height * 0.65f));
        g.drawFittedText (text, 4, 0, width - 6, height, juce::Justification::centredLeft, 1, 0.9f);
    }

    void cellClicked (int row, int columnId, const juce::MouseEvent& e) override
    {
        juce::TableListBoxModel::cellClicked (row, columnId, e);

        if (row >= 0 && row < getNumRows() && e.mods.isPopupMenu())
            owner.createMenuForRow (row).showMenuAsync (juce::PopupMenu::Options()
                                                            .withTargetComponent (e.originalComponent)
                                                            .withMousePosition()
                                                            .withDeletionCheck (owner));
    }

    void deleteKeyPressed (int) override { owner.removeSelectedPlugins(); }

    void sortOrderChanged (int columnId, bool forwards) override
    {
        switch (columnId)
        {
            case nameCol:         list.sort (juce::KnownPluginList::sortAlphabetically, forwards); break;
            case manufacturerCol: list.sort (juce::KnownPluginList::sortByManufacturer, forwards); break;
            case categoryCol:     list.sort (juce::KnownPluginList::sortByCategory, forwards); break;
            case pathCol:         list.sort (juce::KnownPluginList::sortByFileSystemLocation, forwards); break;
            default: break;
        }
    }

private:
    juce::PluginListComponent& owner;
    juce::KnownPluginList& list;
};
} // namespace

//==============================================================================
class PluginListWindow::Content final : public juce::Component
{
public:
    Content (PluginManager& p, AppSettings& s)
        : plugins (p),
          listComponent (p.getFormatManager(), p.getKnownPluginList(), PluginManager::getDeadMansPedalFile(),
                         &s.getProperties(), false)
    {
        auto& table = listComponent.getTableListBox();
        PluginTableModel::setUpColumns (table.getHeader());
        listComponent.setTableModel (new PluginTableModel (listComponent, p.getKnownPluginList()));

        listComponent.setNumberOfThreadsForScanning (kScanThreads);
        listComponent.setScanDialogText ("Escaneando plugins VST3...",
                                         juce::String::fromUTF8 ("Cada plugin se prueba en un proceso aparte."));
        listComponent.setOptionsButtonText ("Opciones...");
        addAndMakeVisible (listComponent);

        scanButton.setButtonText ("Escanear");
        scanButton.onClick = [this] { startScan(); };
        addAndMakeVisible (scanButton);

        hintLabel.setText (juce::String::fromUTF8 ("Clic derecho en una fila: quitar / mostrar carpeta. "
                                                   "Los plugins que fallan al escanear quedan en rojo (desactivados)."),
                           juce::dontSendNotification);
        hintLabel.setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (hintLabel);

        setSize (1000, 560);
    }

    void startScan()
    {
        if (! listComponent.isScanning())
            listComponent.scanFor (plugins.getVst3Format());
    }

    void scanFiles (const juce::StringArray& files)
    {
        if (! listComponent.isScanning())
            listComponent.scanFor (plugins.getVst3Format(), files);
    }

    bool isScanning() const { return listComponent.isScanning(); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6);
        auto bottom = area.removeFromBottom (28);
        scanButton.setBounds (bottom.removeFromRight (110).reduced (0, 2));
        bottom.removeFromRight (6);
        hintLabel.setBounds (bottom);
        area.removeFromBottom (4);
        listComponent.setBounds (area);
    }

private:
    PluginManager& plugins;
    juce::PluginListComponent listComponent;
    juce::TextButton scanButton;
    juce::Label hintLabel;
};

//==============================================================================
PluginListWindow::PluginListWindow (PluginManager& p, AppSettings& s)
    : DocumentWindow ("Plugins VST3",
                      juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::closeButton),
      plugins (p),
      settings (s)
{
    // The scan dialog reads the folders from this key: seed it with default + extra folders.
    juce::PluginListComponent::setLastSearchPath (settings.getProperties(), plugins.getVst3Format(),
                                                  plugins.getSearchPath());

    setUsingNativeTitleBar (true);
    setContentOwned (new Content (plugins, settings), true);
    setResizable (true, false);
    setResizeLimits (600, 300, 4000, 3000);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

PluginListWindow::~PluginListWindow()
{
    syncExtraFolders();
}

void PluginListWindow::closeButtonPressed()
{
    syncExtraFolders();
    setVisible (false);
}

void PluginListWindow::startScan()
{
    if (auto* content = dynamic_cast<Content*> (getContentComponent()))
        content->startScan();
}

void PluginListWindow::scanFiles (const juce::StringArray& files)
{
    if (auto* content = dynamic_cast<Content*> (getContentComponent()))
        content->scanFiles (files);
}

bool PluginListWindow::isScanning() const
{
    if (auto* content = dynamic_cast<Content*> (getContentComponent()))
        return content->isScanning();

    return false;
}

void PluginListWindow::syncExtraFolders()
{
    plugins.setExtraFoldersFromFullPath (
        juce::PluginListComponent::getLastSearchPath (settings.getProperties(), plugins.getVst3Format()));
    plugins.save();
}

} // namespace vstplayer
