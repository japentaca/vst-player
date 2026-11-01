#include "app/MainWindow.h"

#include "app/AppSettings.h"

namespace vstplayer
{

namespace
{
constexpr int kDefaultWidth = 1024;
constexpr int kDefaultHeight = 640;
constexpr int kMinWidth = 640;
constexpr int kMinHeight = 400;

// Empty placeholder content until the real UI arrives in later stages.
class EmptyContent final : public juce::Component
{
public:
    EmptyContent() { setSize (kDefaultWidth, kDefaultHeight); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
        g.setColour (juce::Colours::grey);
        g.setFont (16.0f);
        g.drawText ("Sin proyecto cargado", getLocalBounds(), juce::Justification::centred);
    }
};
} // namespace

MainWindow::MainWindow (const juce::String& title, AppSettings& s)
    : DocumentWindow (title,
                      juce::Desktop::getInstance().getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons),
      settings (s)
{
    setUsingNativeTitleBar (true);
    setMenuBar (this);
    setContentOwned (new EmptyContent(), true);
    setResizable (true, true);
    setResizeLimits (kMinWidth, kMinHeight, 10000, 10000);

    const auto savedState = settings.getMainWindowState();
    if (savedState.isEmpty() || ! restoreWindowStateFromString (savedState))
        centreWithSize (kDefaultWidth, kDefaultHeight);

    setVisible (true);
}

MainWindow::~MainWindow()
{
    setMenuBar (nullptr);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void MainWindow::saveWindowState()
{
    settings.setMainWindowState (getWindowStateAsString());
}

juce::StringArray MainWindow::getMenuBarNames()
{
    return { "Archivo", "Audio", "Ayuda" };
}

juce::PopupMenu MainWindow::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    switch (topLevelMenuIndex)
    {
        case fileMenu:
            menu.addItem (cmdQuit, juce::String::fromUTF8 ("Salir"));
            break;

        case audioMenu:
            // Filled in stage 2 (audio device settings) and stage 4 (MIDI ports).
            menu.addItem (-1, juce::String::fromUTF8 ("(sin opciones todavía)"), false);
            break;

        case helpMenu:
            menu.addItem (cmdAbout, juce::String::fromUTF8 ("Acerca de VstPlayer…"));
            break;

        default:
            break;
    }

    return menu;
}

void MainWindow::menuItemSelected (int menuItemID, int)
{
    switch (menuItemID)
    {
        case cmdQuit:
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            break;

        case cmdAbout:
            showAboutBox();
            break;

        default:
            break;
    }
}

void MainWindow::showAboutBox()
{
    const auto message = "VstPlayer " + juce::JUCEApplication::getInstance()->getApplicationVersion()
                         + "\n" + juce::String::fromUTF8 ("Host de instrumentos VST3.")
                         + "\n" + juce::SystemStats::getJUCEVersion();

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                                            "Acerca de VstPlayer",
                                            message,
                                            "Aceptar",
                                            this);
}

} // namespace vstplayer
