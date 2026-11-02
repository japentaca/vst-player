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
                text = juce::String::fromUTF8 ("Desactivado (falló al escanear)");
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
namespace
{
// One row per search folder: the default VST3 folders first (fixed), then the extra ones.
class FolderListModel final : public juce::ListBoxModel
{
public:
    struct Row
    {
        juce::File folder;
        bool isDefault = false;
    };

    std::vector<Row> rows;
    std::function<void()> onSelectionChanged;

    void refresh (PluginManager& plugins)
    {
        rows.clear();
        const auto defaults = plugins.getDefaultFolders();
        const auto extras = plugins.getExtraFolders();

        for (int i = 0; i < defaults.getNumPaths(); ++i)
            rows.push_back ({ defaults[i], true });

        for (int i = 0; i < extras.getNumPaths(); ++i)
            rows.push_back ({ extras[i], false });
    }

    int getNumRows() override { return (int) rows.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()))
            return;

        const auto& r = rows[(size_t) row];
        auto& lf = juce::LookAndFeel::getDefaultLookAndFeel();
        const auto text = lf.findColour (juce::ListBox::textColourId);

        if (selected)
            g.fillAll (lf.findColour (juce::ListBox::backgroundColourId).interpolatedWith (text, 0.25f));

        juce::String tag;

        if (r.isDefault)
            tag = juce::String::fromUTF8 ("predeterminada");

        if (! r.folder.isDirectory())
            tag = tag.isEmpty() ? "no existe ahora" : tag + ", no existe";

        const auto font = juce::FontOptions ((float) height * 0.6f);
        auto area = juce::Rectangle<int> (0, 0, width, height).reduced (6, 0);

        if (tag.isNotEmpty())
        {
            const auto tagText = "(" + tag + ")";
            const auto tagWidth = juce::GlyphArrangement::getStringWidthInt (juce::Font (font), tagText) + 8;
            g.setColour (r.folder.isDirectory() ? text.withAlpha (0.55f) : juce::Colours::orange);
            g.setFont (font);
            g.drawText (tagText, area.removeFromRight (tagWidth), juce::Justification::centredRight, false);
        }

        g.setColour (r.folder.isDirectory() ? text : text.withAlpha (0.6f));
        g.setFont (font);
        g.drawFittedText (r.folder.getFullPathName(), area, juce::Justification::centredLeft, 1, 0.9f);
    }

    void selectedRowsChanged (int) override
    {
        if (onSelectionChanged)
            onSelectionChanged();
    }
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
        foldersLabel.setText (juce::String::fromUTF8 ("Carpetas de búsqueda (se recorren con sus subcarpetas)"),
                              juce::dontSendNotification);
        foldersLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        addAndMakeVisible (foldersLabel);

        folderModel.onSelectionChanged = [this] { updateButtons(); };
        folderList.setModel (&folderModel);
        folderList.setRowHeight (24);
        folderList.setOutlineThickness (1);
        addAndMakeVisible (folderList);

        addFolderButton.setButtonText (juce::String::fromUTF8 ("Agregar carpeta…"));
        addFolderButton.onClick = [this] { chooseFolder(); };
        addAndMakeVisible (addFolderButton);

        removeFolderButton.setButtonText ("Quitar carpeta");
        removeFolderButton.onClick = [this]
        {
            const auto row = folderList.getSelectedRow();

            if (juce::isPositiveAndBelow (row, (int) folderModel.rows.size()) && ! folderModel.rows[(size_t) row].isDefault)
                removeFolder (folderModel.rows[(size_t) row].folder);
        };
        addAndMakeVisible (removeFolderButton);

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

        statusLabel.setFont (juce::FontOptions (13.0f));
        statusLabel.setMinimumHorizontalScale (0.8f);
        addAndMakeVisible (statusLabel);
        setStatus (juce::String::fromUTF8 ("Clic derecho en una fila: quitar / mostrar carpeta. "
                                           "Los plugins que fallan al escanear quedan en rojo (desactivados)."));

        refreshFolders();
        setSize (1000, 680);
    }

    ~Content() override
    {
        folderList.setModel (nullptr);
    }

    void startScan()
    {
        if (listComponent.isScanning())
            return;

        const auto path = plugins.getSearchPath();
        const auto files = plugins.findPluginFiles (path);

        if (files.isEmpty())
        {
            // scanFor() with an empty list would fall back to JUCE's folder dialog.
            setStatus (juce::String::fromUTF8 ("No hay archivos .vst3 en las carpetas de búsqueda."));
            return;
        }

        setStatus ("Escaneando " + juce::String (files.size()) + " archivos .vst3 en " + juce::String (path.getNumPaths())
                   + " carpetas (los ya conocidos y sin cambios se saltean).");
        listComponent.scanFor (plugins.getVst3Format(), files);
    }

    void scanFiles (const juce::StringArray& files)
    {
        if (! listComponent.isScanning() && ! files.isEmpty())
            listComponent.scanFor (plugins.getVst3Format(), files);
    }

    bool isScanning() const { return listComponent.isScanning(); }

    bool addFolder (const juce::File& folder)
    {
        if (listComponent.isScanning())
        {
            setStatus (juce::String::fromUTF8 ("Esperá a que termine el escaneo en curso."));
            return false;
        }

        const auto result = plugins.addExtraFolder (folder);
        refreshFolders();

        if (result != PluginManager::AddFolderResult::added)
        {
            setStatus (PluginManager::describe (result, folder));
            return false;
        }

        for (int i = 0; i < (int) folderModel.rows.size(); ++i)
            if (folderModel.rows[(size_t) i].folder == folder)
                folderList.selectRow (i);

        juce::FileSearchPath onlyThis;
        onlyThis.add (folder);
        const auto files = plugins.findPluginFiles (onlyThis);

        if (files.isEmpty())
        {
            setStatus (PluginManager::describe (result, folder)
                       + juce::String::fromUTF8 (". No tiene archivos .vst3 todavía; se va a escanear cuando los tenga (botón Escanear)."));
            return true;
        }

        setStatus (PluginManager::describe (result, folder) + ". Escaneando " + juce::String (files.size()) + " archivos .vst3...");
        listComponent.scanFor (plugins.getVst3Format(), files);
        return true;
    }

    int removeFolder (const juce::File& folder)
    {
        if (listComponent.isScanning())
        {
            setStatus (juce::String::fromUTF8 ("Esperá a que termine el escaneo en curso."));
            return -1;
        }

        const auto removed = plugins.removeExtraFolder (folder);
        refreshFolders();

        if (removed < 0)
            setStatus ("No es una carpeta agregada: " + folder.getFullPathName());
        else
            setStatus ("Carpeta quitada: " + folder.getFullPathName() + " ("
                       + (removed == 1 ? juce::String ("1 entrada quitada") : juce::String (removed) + " entradas quitadas")
                       + " de la lista de plugins).");

        return removed;
    }

    int getNumFolderRows() const { return (int) folderModel.rows.size(); }
    juce::String getStatusText() const { return statusLabel.getText(); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6);

        foldersLabel.setBounds (area.removeFromTop (22));
        auto folders = area.removeFromTop (4 * 24 + 2);
        auto buttons = folders.removeFromRight (150);
        folders.removeFromRight (6);
        folderList.setBounds (folders);
        addFolderButton.setBounds (buttons.removeFromTop (28));
        buttons.removeFromTop (6);
        removeFolderButton.setBounds (buttons.removeFromTop (28));
        area.removeFromTop (8);

        auto bottom = area.removeFromBottom (28);
        scanButton.setBounds (bottom.removeFromRight (110).reduced (0, 2));
        bottom.removeFromRight (6);
        statusLabel.setBounds (bottom);
        area.removeFromBottom (4);
        listComponent.setBounds (area);
    }

private:
    void setStatus (const juce::String& text)
    {
        statusLabel.setText (text, juce::dontSendNotification);
        statusLabel.setTooltip (text);
    }

    void refreshFolders()
    {
        folderModel.refresh (plugins);
        folderList.updateContent();
        folderList.repaint();
        updateButtons();
    }

    void updateButtons()
    {
        const auto row = folderList.getSelectedRow();
        removeFolderButton.setEnabled (juce::isPositiveAndBelow (row, (int) folderModel.rows.size())
                                       && ! folderModel.rows[(size_t) row].isDefault);
    }

    void chooseFolder()
    {
        auto start = juce::File::getSpecialLocation (juce::File::windowsProgramFilesCommon);
        const auto row = folderList.getSelectedRow();

        if (juce::isPositiveAndBelow (row, (int) folderModel.rows.size()) && folderModel.rows[(size_t) row].folder.isDirectory())
            start = folderModel.rows[(size_t) row].folder;

        chooser = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("Elegí una carpeta con plugins VST3"), start);
        juce::Component::SafePointer<Content> safeThis (this);

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safeThis] (const juce::FileChooser& fc)
                              {
                                  if (safeThis == nullptr)
                                      return;

                                  const auto folder = fc.getResult();

                                  if (folder != juce::File())
                                      safeThis->addFolder (folder);
                              });
    }

    PluginManager& plugins;
    juce::Label foldersLabel;
    FolderListModel folderModel;
    juce::ListBox folderList;
    juce::TextButton addFolderButton, removeFolderButton;
    juce::PluginListComponent listComponent;
    juce::TextButton scanButton;
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================
PluginListWindow::PluginListWindow (PluginManager& p, AppSettings& s)
    : DocumentWindow ("Plugins VST3",
                      juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::closeButton),
      plugins (p),
      settings (s)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new Content (plugins, settings), true);
    setResizable (true, false);
    setResizeLimits (700, 480, 4000, 3000);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

PluginListWindow::~PluginListWindow()
{
    plugins.save();
}

void PluginListWindow::closeButtonPressed()
{
    plugins.save();
    setVisible (false);
}

PluginListWindow::Content* PluginListWindow::getContent() const
{
    return dynamic_cast<Content*> (getContentComponent());
}

void PluginListWindow::startScan()
{
    if (auto* content = getContent())
        content->startScan();
}

void PluginListWindow::scanFiles (const juce::StringArray& files)
{
    if (auto* content = getContent())
        content->scanFiles (files);
}

bool PluginListWindow::isScanning() const
{
    if (auto* content = getContent())
        return content->isScanning();

    return false;
}

bool PluginListWindow::addFolder (const juce::File& folder)
{
    if (auto* content = getContent())
        return content->addFolder (folder);

    return false;
}

int PluginListWindow::removeFolder (const juce::File& folder)
{
    if (auto* content = getContent())
        return content->removeFolder (folder);

    return -1;
}

int PluginListWindow::getNumFolderRows() const
{
    if (auto* content = getContent())
        return content->getNumFolderRows();

    return 0;
}

juce::String PluginListWindow::getStatusText() const
{
    if (auto* content = getContent())
        return content->getStatusText();

    return {};
}

} // namespace vstplayer
