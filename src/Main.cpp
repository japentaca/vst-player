#include <juce_gui_extra/juce_gui_extra.h>

#include "app/AppSettings.h"
#include "app/MainWindow.h"

namespace vstplayer
{

class VstPlayerApplication final : public juce::JUCEApplication
{
public:
    VstPlayerApplication() = default;

    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        settings = std::make_unique<AppSettings>();
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *settings);
    }

    void shutdown() override
    {
        // Every quit path (close button, Archivo > Salir, WM_CLOSE, logoff) ends here.
        if (mainWindow != nullptr)
            mainWindow->saveWindowState();

        mainWindow = nullptr;

        if (settings != nullptr)
            settings->flush();

        settings = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr)
            mainWindow->toFront (true);
    }

private:
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace vstplayer

START_JUCE_APPLICATION (vstplayer::VstPlayerApplication)
