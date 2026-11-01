// "--ui-selftest": stage 6 checks (definitive UI). See InstanceSelfTest.h.
#include "app/InstanceSelfTest.h"

#include "app/CommandLine.h"
#include "app/MainWindow.h"
#include "engine/AudioEngine.h"
#include "engine/GainProcessor.h"
#include "engine/InstanceGraph.h"
#include "engine/MidiRouter.h"
#include "engine/PluginManager.h"
#include "project/ProjectController.h"
#include "ui/AddInstanceDialog.h"
#include "ui/InstancePanel.h"
#include "ui/MainContent.h"
#include "ui/PluginListWindow.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <memory>
#include <typeinfo>
#include <vector>

namespace vstplayer
{

namespace
{
constexpr float kTestGainDb = -30.0f;
constexpr double kLoadTimeoutSeconds = 90.0;

// Measures how long the message thread goes without running a 5 ms timer: the longest gap is
// the longest time the UI could not repaint or react to input.
class GapMonitor final : private juce::Timer
{
public:
    void begin()
    {
        maxGapMs = 0.0;
        ticks = 0;
        tickTimes.clear();
        lastMs = juce::Time::getMillisecondCounterHiRes();
        startTimer (5);
    }

    void end()
    {
        timerCallback();
        stopTimer();
    }

    double maxGapMs = 0.0;
    int ticks = 0;
    double lastMs = 0.0;
    double lastTickMs = 0.0;
    std::vector<double> tickTimes;

    int ticksBefore (double timeMs) const
    {
        return (int) std::count_if (tickTimes.begin(), tickTimes.end(), [timeMs] (double t) { return t < timeMs; });
    }

private:
    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        maxGapMs = juce::jmax (maxGapMs, now - lastMs);
        lastMs = now;
        lastTickMs = now;
        tickTimes.push_back (now);
        ++ticks;
    }
};

struct UiTestState
{
    juce::PluginDescription dexed, surge, sforzando;
    InstanceId dexedId, surgeId, sforzandoId, copyId;
    juce::File dir;
    juce::File projectFile;
    GapMonitor gaps;
    double addedAtMs = 0.0;
    int ticksBeforeCreation = -1;
    juce::String statusBefore;
    juce::MemoryBlock dexedState;
    std::unique_ptr<juce::XmlElement> pluginListBackup;
    int pluginTypesBefore = 0;
    juce::StringArray scanFiles;
    bool progressSeenDuringScan = false;
    int scanGapChecks = 0;
    int savedFileWrites = 0;
    juce::Time savedTime;
};

std::unique_ptr<UiTestState>& uiStatePointer()
{
    static std::unique_ptr<UiTestState> s;
    return s;
}

UiTestState& uiState()
{
    auto& s = uiStatePointer();

    if (s == nullptr)
        s = std::make_unique<UiTestState>();

    return *s;
}

// Screenshot of a top-level window as Windows draws it (PrintWindow, full content), physical pixels.
juce::Image captureWindow (juce::Component& window)
{
    auto* hwnd = (HWND) window.getWindowHandle();

    if (hwnd == nullptr)
        return {};

    RECT r {};
    ::GetWindowRect (hwnd, &r);
    const auto w = (int) (r.right - r.left), h = (int) (r.bottom - r.top);

    if (w <= 0 || h <= 0)
        return {};

    auto* screen = ::GetDC (nullptr);
    auto* mem = ::CreateCompatibleDC (screen);
    auto* bitmap = ::CreateCompatibleBitmap (screen, w, h);
    auto* old = ::SelectObject (mem, bitmap);
    constexpr UINT kRenderFullContent = 2; // PW_RENDERFULLCONTENT
    ::PrintWindow (hwnd, mem, kRenderFullContent);

    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h; // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    juce::HeapBlock<juce::uint32> pixels ((size_t) w * (size_t) h);
    ::SelectObject (mem, old);
    ::GetDIBits (mem, bitmap, 0, (UINT) h, pixels.get(), &info, DIB_RGB_COLORS);
    ::DeleteObject (bitmap);
    ::DeleteDC (mem);
    ::ReleaseDC (nullptr, screen);

    juce::Image image (juce::Image::RGB, w, h, false);
    juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            data.setPixelColour (x, y, juce::Colour ((juce::uint32) (pixels[(size_t) y * (size_t) w + (size_t) x] | 0xff000000u)));

    return image;
}

bool saveScreenshot (juce::Component& window, const juce::File& file)
{
    const auto image = captureWindow (window);

    if (! image.isValid())
        return false;

    file.deleteFile();
    juce::FileOutputStream out (file);
    return out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out);
}

// Problems of a laid-out component tree: children outside their parent, overlapping visible
// siblings, controls too small, button texts that do not fit (they would be squeezed or wrapped).
void checkLayout (juce::Component& c, juce::StringArray& problems, const juce::String& path)
{
    const auto describe = [] (juce::Component& comp)
    {
        if (auto* b = dynamic_cast<juce::Button*> (&comp))
            return "boton \"" + b->getButtonText() + "\"";
        if (auto* l = dynamic_cast<juce::Label*> (&comp))
            return "label \"" + l->getText().substring (0, 30) + "\"";
        if (dynamic_cast<juce::ComboBox*> (&comp) != nullptr)
            return juce::String ("combo");
        if (dynamic_cast<juce::Slider*> (&comp) != nullptr)
            return juce::String ("slider");
        return comp.getName().isNotEmpty() ? comp.getName() : juce::String (typeid (comp).name()).fromLastOccurrenceOf (":", false, false);
    };

    // A viewport's content may be larger than the viewport: that is what scrolling is for.
    const auto isViewportPart = dynamic_cast<juce::Viewport*> (&c) != nullptr
                                || dynamic_cast<juce::Viewport*> (c.getParentComponent()) != nullptr;
    juce::Array<juce::Component*> visibleChildren;

    for (auto* child : c.getChildren())
    {
        if (! child->isVisible())
            continue;

        visibleChildren.add (child);
        const auto name = path + "/" + describe (*child);

        if (! isViewportPart && ! c.getLocalBounds().contains (child->getBounds()))
            problems.add (name + " fuera del padre: " + child->getBounds().toString() + " en " + c.getLocalBounds().toString());

        const auto interactive = dynamic_cast<juce::Button*> (child) != nullptr || dynamic_cast<juce::ComboBox*> (child) != nullptr
                                 || dynamic_cast<juce::Slider*> (child) != nullptr;

        if (interactive && (child->getWidth() < 24 || child->getHeight() < 18))
            problems.add (name + " demasiado chico: " + juce::String (child->getWidth()) + "x" + juce::String (child->getHeight()));

        if (auto* tb = dynamic_cast<juce::TextButton*> (child))
        {
            const auto font = tb->getLookAndFeel().getTextButtonFont (*tb, tb->getHeight());
            const auto cornerSize = juce::jmin (tb->getHeight(), tb->getWidth()) / 2;
            const auto fontHeight = juce::roundToInt (font.getHeight() * 0.6f);
            const auto indent = juce::jmin (fontHeight, 2 + cornerSize / 2);
            const auto available = tb->getWidth() - 2 * indent;
            const auto needed = juce::GlyphArrangement::getStringWidth (font, tb->getButtonText());

            if (needed > (float) available)
                problems.add (name + ": el texto no entra (" + juce::String (needed, 0) + " px de " + juce::String (available) + ")");
        }

        if (auto* toggle = dynamic_cast<juce::ToggleButton*> (child))
        {
            const auto needed = juce::GlyphArrangement::getStringWidth (juce::FontOptions (juce::jmin (15.0f, (float) toggle->getHeight() * 0.75f)),
                                                                        toggle->getButtonText());
            const auto available = (float) toggle->getWidth() - juce::jmin (25.0f, (float) toggle->getHeight() * 0.75f) - 8.0f;

            if (needed > available)
                problems.add (name + ": el texto no entra (" + juce::String (needed, 0) + " px de " + juce::String (available, 0) + ")");
        }

        checkLayout (*child, problems, name);
    }

    for (int i = 0; i < visibleChildren.size(); ++i)
        for (int j = i + 1; j < visibleChildren.size(); ++j)
            if (! isViewportPart && visibleChildren[i]->getBounds().intersects (visibleChildren[j]->getBounds()))
                problems.add (path + ": se superponen " + describe (*visibleChildren[i]) + " " + visibleChildren[i]->getBounds().toString()
                              + " y " + describe (*visibleChildren[j]) + " " + visibleChildren[j]->getBounds().toString());
}

// Sets the window's outer size (title bar and borders included) in logical pixels.
void setOuterSize (juce::DocumentWindow& window, int width, int height)
{
    juce::BorderSize<int> frame;

    if (auto* peer = window.getPeer())
        if (auto f = peer->getFrameSizeIfPresent())
            frame = *f;

    window.setSize (width - frame.getLeftAndRight(), height - frame.getTopAndBottom());
}

juce::String describeOuterSize (juce::DocumentWindow& window)
{
    RECT r {};
    ::GetWindowRect ((HWND) window.getWindowHandle(), &r);
    const auto scale = window.getPeer() != nullptr ? window.getPeer()->getPlatformScaleFactor() : 1.0;
    return juce::String ((int) (r.right - r.left)) + "x" + juce::String ((int) (r.bottom - r.top)) + " px fisicos, ventana "
           + juce::String (window.getWidth()) + "x" + juce::String (window.getHeight()) + " logicos (sin marco), escala de plataforma "
           + juce::String (scale, 2) + ", escala global JUCE " + juce::String (juce::Desktop::getInstance().getGlobalScaleFactor(), 2);
}

bool sendKeyToWindow (juce::Component& window, const juce::KeyPress& key)
{
    auto* peer = window.getPeer();
    return peer != nullptr && peer->handleKeyPress (key);
}

// Real keyboard input through the OS (only possible when our window is in the foreground).
bool sendOsKeyPress (WORD virtualKey, bool ctrl)
{
    std::vector<INPUT> inputs;
    const auto add = [&inputs] (WORD vk, bool up)
    {
        INPUT in {};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        in.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        inputs.push_back (in);
    };

    if (ctrl)
        add (VK_CONTROL, false);

    add (virtualKey, false);
    add (virtualKey, true);

    if (ctrl)
        add (VK_CONTROL, true);

    return ::SendInput ((UINT) inputs.size(), inputs.data(), sizeof (INPUT)) == inputs.size();
}
} // namespace

void InstanceSelfTest::clearUiTestState()
{
    uiStatePointer() = nullptr;
}

//==============================================================================
void InstanceSelfTest::buildUiSteps()
{
    auto& panel = window.getInstancePanel();
    auto& content = window.getMainContent();
    auto& keys = panel.getKeyboardState();
    auto& s = uiState();
    s.dir = projectTestDir;
    s.projectFile = projectTestDir.getChildFile ("ui-prueba.vstproj.json");

    const auto shot = [this, &s] (const juce::String& name, juce::Component* target = nullptr)
    {
        action ("Captura " + name, [this, &s, name, target]
        {
            auto& comp = target != nullptr ? *target : static_cast<juce::Component&> (window);
            const auto file = s.dir.getChildFile (name + ".png");
            return check (saveScreenshot (comp, file), "captura " + file.getFullPathName() + " (" + describeOuterSize (window) + ")");
        });
    };

    const auto layoutCheck = [this, &content] (const juce::String& label, bool mustPass)
    {
        action ("Layout " + label, [this, &content, label, mustPass]
        {
            juce::StringArray problems;
            checkLayout (content, problems, "MainContent");

            for (auto& p : problems)
                cli::print ("    " + juce::String (mustPass ? "" : "(aviso) ") + p);

            return check (! mustPass || problems.isEmpty(),
                          label + ": " + juce::String (problems.size()) + " problemas de layout (" + describeOuterSize (window) + ")");
        });
    };

    const auto loadedOk = [this] (const InstanceId& id)
    {
        auto* instance = graph.findInstance (id);
        return instance != nullptr && instance->state != Instance::State::loading;
    };

    // Adds a plugin through the dialog (search + select + "Agregar") while measuring the gaps.
    const auto addThroughDialog = [this, &panel, &s, loadedOk] (const juce::String& search, InstanceId* target, const juce::String& label)
    {
        action ("Agregar " + label + " desde el dialogo (busqueda \"" + search + "\")", [this, &panel, &s, search, target]
        {
            panel.showAddDialog();
            auto* dialog = panel.getAddDialog();

            if (! check (dialog != nullptr && dialog->isVisible(), "dialogo \"Agregar instancia\" abierto"))
                return false;

            dialog->setSearchText (search);
            const auto listed = dialog->getListedPlugins();
            auto ok = check (listed.size() == 1, "busqueda \"" + search + "\": " + juce::String (listed.size()) + " resultado(s)"
                                                     + (listed.isEmpty() ? juce::String() : ": " + listed[0].name));
            ok = check (dialog->selectPlugin (0), "seleccionar el resultado") && ok;

            s.gaps.begin();
            s.addedAtMs = juce::Time::getMillisecondCounterHiRes();
            const auto before = graph.getInstances().size();
            ok = check (dialog->addSelected(), "\"Agregar\"") && ok;
            ok = check (graph.getInstances().size() == before + 1, "instancia nueva en la lista") && ok;

            if (graph.getInstances().size() > before)
                *target = graph.getInstances().back()->id;

            const auto info = panel.getRowInfo (*target);
            ok = check (info.exists && info.loadingIndicator && info.stateText.contains ("cargando"),
                        "la fila aparece enseguida con indicador de carga: \"" + info.stateText + "\"")
                 && ok;
            graph.setGainDb (*target, kTestGainDb);
            return ok;
        });

        waitFor ("esperar la carga de " + label, [loadedOk, target] { return loadedOk (*target); }, kLoadTimeoutSeconds);

        action ("Hilo de mensajes durante la carga de " + label, [this, &panel, &s, target, label]
        {
            s.gaps.end();
            const auto creationStart = graph.getLastCreationStartMs();
            const auto waitedBeforeCreation = creationStart - s.addedAtMs;
            const auto* instance = graph.findInstance (*target);
            auto ok = check (instance != nullptr && instance->isReady(), label + " listo");
            ok = check (panel.getAddDialog() == nullptr, "el dialogo se cerro") && ok;

            cli::print ("    carga de " + label + ": DLL precargada en otro hilo en " + juce::String (graph.getLastPreloadMs(), 0)
                        + " ms; creacion en el hilo de mensajes (inevitable en VST3) " + juce::String (graph.getLastCreationMs(), 0)
                        + " ms; el hilo de mensajes corrio " + juce::String (waitedBeforeCreation, 0)
                        + " ms antes de empezar a crear; ticks del timer de 5 ms durante toda la carga: " + juce::String (s.gaps.ticks)
                        + "; hueco maximo " + juce::String (s.gaps.maxGapMs, 0) + " ms");

            // The UI kept running until the creation itself: the row was painted, the dialog closed.
            const auto ticksBefore = s.gaps.ticksBefore (creationStart);
            ok = check (ticksBefore >= 2, "la UI corrio antes de la creacion (" + juce::String (waitedBeforeCreation, 0) + " ms, "
                                              + juce::String (ticksBefore) + " ticks del timer)")
                 && ok;
            // The only stall is the plugin's own creation on the message thread.
            ok = check (s.gaps.maxGapMs <= graph.getLastCreationMs() + 100.0,
                        "el unico bloqueo es la creacion del plugin (hueco " + juce::String (s.gaps.maxGapMs, 0) + " ms <= creacion "
                            + juce::String (graph.getLastCreationMs(), 0) + " ms + 100)")
                 && ok;
            return ok;
        });
    };

    //==============================================================================
    action ("Preparar: ventana 1024x600, plugins de prueba, dialogos de archivo simulados", [this, &s]
    {
        for (auto& d : plugins.getKnownPluginList().getTypes())
        {
            if (d.name == "Dexed") s.dexed = d;
            if (d.name == "Surge XT") s.surge = d;
            if (d.name == "sforzando") s.sforzando = d;
        }

        s.dir.createDirectory();
        window.fileChooserOverride = [&s] (bool) { return s.projectFile; };
        setOuterSize (window, 1024, 600);
        window.toFront (true);

        for (auto& d : juce::Desktop::getInstance().getDisplays().displays)
            cli::print ("    pantalla: " + d.totalArea.toString() + " logicos, escala " + juce::String (d.scale, 2) + ", dpi "
                        + juce::String (d.dpi, 0) + (d.isMain ? " (principal)" : ""));

        return check (s.dexed.name.isNotEmpty() && s.surge.name.isNotEmpty() && s.sforzando.name.isNotEmpty(),
                      "Dexed, Surge XT y sforzando en la lista de plugins");
    });

    waitMs (600);
    layoutCheck ("1024x600 vacia", true);
    shot ("01-vacia-1024x600");

    action ("Barra superior: audio y CPU", [this, &content]
    {
        auto ok = check (content.getAudioText().contains ("Hz") && content.getAudioText().contains ("buffer"),
                         "audio: \"" + content.getAudioText() + "\"");
        ok = check (content.getCpuText().startsWith ("CPU") && content.getCpuText().endsWith ("%"), "medidor: \"" + content.getCpuText() + "\"") && ok;
        ok = check (window.getProjectInfoText().contains (juce::String::fromUTF8 ("Sin título")), "proyecto: " + window.getProjectInfoText()) && ok;
        return ok;
    });

    //==============================================================================
    action ("Dialogo Agregar instancia: filtro, agrupado, busqueda", [this, &panel]
    {
        panel.showAddDialog();
        auto* dialog = panel.getAddDialog();

        if (! check (dialog != nullptr, "dialogo abierto"))
            return false;

        auto ok = check (dialog->isInstrumentsOnly(), "\"Solo instrumentos\" activo por defecto");
        const auto instruments = dialog->getListedPlugins();
        auto allInstruments = true;

        for (auto& d : instruments)
            allInstruments = allInstruments && d.isInstrument;

        ok = check (allInstruments && instruments.size() > 0, juce::String (instruments.size()) + " instrumentos, todos isInstrument") && ok;

        const auto groups = dialog->getListedManufacturers();
        auto sorted = groups;
        sorted.sortNatural();
        ok = check (groups == sorted && groups.size() > 1, "agrupados por fabricante: " + groups.joinIntoString (" | ")) && ok;

        dialog->setInstrumentsOnly (false);
        const auto all = dialog->getListedPlugins();
        ok = check (all.size() == plugins.getKnownPluginList().getNumTypes() && all.size() > instruments.size(),
                    "sin filtro: " + juce::String (all.size()) + " plugins (efectos incluidos)")
             && ok;

        dialog->setSearchText ("surge");
        juce::StringArray names;
        for (auto& d : dialog->getListedPlugins())
            names.add (d.name);
        ok = check (names.contains ("Surge XT") && names.contains ("Surge XT Effects"), "\"surge\" sin filtro: " + names.joinIntoString (", ")) && ok;

        dialog->setInstrumentsOnly (true);
        names.clear();
        for (auto& d : dialog->getListedPlugins())
            names.add (d.name);
        ok = check (names == juce::StringArray { "Surge XT" }, "\"surge\" solo instrumentos: " + names.joinIntoString (", ")) && ok;

        dialog->setSearchText ("plogue");
        ok = check (dialog->getListedPlugins().size() == 1 && dialog->getListedPlugins()[0].name == "sforzando",
                    "busqueda por fabricante \"plogue\" -> sforzando")
             && ok;

        dialog->setSearchText ({});
        return ok;
    });

    waitMs (400);
    action ("Captura del dialogo", [this, &panel, &s]
    {
        auto* dialog = panel.getAddDialog();
        return check (dialog != nullptr && saveScreenshot (*dialog, s.dir.getChildFile ("02-dialogo-agregar.png")), "captura 02-dialogo-agregar.png");
    });

    action ("Traer el dialogo al frente", [&panel]
    {
        if (auto* dialog = panel.getAddDialog())
            dialog->toFront (true);

        return true;
    });

    // Like the OS does, key presses go to the focused component: wait until it is in the dialog.
    waitFor ("foco en el dialogo", [&panel]
    {
        auto* dialog = panel.getAddDialog();
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        return dialog != nullptr && focused != nullptr && dialog->isParentOf (focused);
    }, 5.0);

    action ("Cerrar el dialogo con Esc", [this, &panel]
    {
        auto* dialog = panel.getAddDialog();

        if (dialog == nullptr)
            return false;

        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        cli::print ("    foco en: " + (focused != nullptr ? juce::String (typeid (*focused).name()) : juce::String ("(nada)")));
        auto* peer = dialog->getPeer();
        const auto handled = peer != nullptr && peer->handleKeyPress (juce::KeyPress (juce::KeyPress::escapeKey));
        return check (handled, "Esc procesado");
    });

    // The text editor reports Esc to its listeners asynchronously.
    waitFor ("Esc cierra el dialogo", [&panel] { return panel.getAddDialog() == nullptr; }, 3.0);

    waitMs (200);

    //==============================================================================
    addThroughDialog ("surge xt", &s.surgeId, "Surge XT");
    addThroughDialog ("sforzando", &s.sforzandoId, "sforzando");
    addThroughDialog ("dexed", &s.dexedId, "Dexed");

    waitMs (500);
    layoutCheck ("1024x600 con 3 instancias", true);
    shot ("03-tres-instancias-1024x600");

    //==============================================================================
    // Two loads queued at once: the message thread runs between them.
    action ("Dos cargas a la vez (Dexed y sforzando): la UI corre entre una y otra", [this, &panel, &s]
    {
        s.gaps.begin();
        s.copyId = panel.addInstance (s.dexed);
        const auto second = panel.addInstance (s.sforzando);
        graph.setGainDb (s.copyId, kTestGainDb);
        graph.setGainDb (second, kTestGainDb);
        return check (graph.getNumLoading() == 2, "2 instancias cargando");
    });

    waitFor ("esperar las dos cargas", [this] { return graph.getNumLoading() == 0 && ! graph.isCreationPending(); }, kLoadTimeoutSeconds);

    action ("Quitar las dos instancias extra", [this, &panel, &s]
    {
        s.gaps.end();
        cli::print ("    dos cargas: hueco maximo " + juce::String (s.gaps.maxGapMs, 0) + " ms, creacion mas lenta hasta ahora "
                    + juce::String (graph.getMaxCreationMs(), 0) + " ms, ticks " + juce::String (s.gaps.ticks));
        auto ok = check (s.gaps.maxGapMs <= graph.getMaxCreationMs() + 100.0, "sin bloqueos mas largos que una creacion");

        while (graph.getInstances().size() > 3)
            panel.removeInstance (graph.getInstances().back()->id);

        s.copyId.clear();
        ok = check (graph.getInstances().size() == 3, "quedan 3 instancias") && ok;
        return ok;
    });

    //==============================================================================
    action ("Dexed: programa 5 y entrada MIDI antes de duplicar", [this, &panel, &s]
    {
        auto* instance = graph.findInstance (s.dexedId);
        auto* plugin = instance != nullptr ? instance->getPlugin() : nullptr;

        if (plugin == nullptr)
            return check (false, "Dexed no esta listo");

        plugin->setCurrentProgram (5);
        graph.setMute (s.dexedId, false);

        const auto ports = router.getAvailableInputNames();
        if (! ports.isEmpty())
        {
            panel.setInstanceMidiPort (s.dexedId, ports[0]);
            panel.setInstanceMidiChannels (s.dexedId, (juce::uint16) 0x0005); // channels 1 and 3
            panel.setInstanceMidiRemap (s.dexedId, 2);
        }

        s.dexedState.reset();
        plugin->getStateInformation (s.dexedState);
        return check (s.dexedState.getSize() > 0, "estado de Dexed: " + juce::String ((int) s.dexedState.getSize()) + " bytes, puertos MIDI: "
                                                       + (ports.isEmpty() ? juce::String ("ninguno") : ports[0]));
    });

    action ("Duplicar Dexed (boton Duplicar)", [this, &panel, &s]
    {
        auto ok = check (panel.getRowInfo (s.dexedId).duplicateEnabled, "boton Duplicar habilitado");
        s.copyId = panel.duplicateInstance (s.dexedId);
        return check (s.copyId.isNotEmpty(), "copia creada") && ok;
    });

    waitFor ("esperar la copia", [loadedOk, &s] { return loadedOk (s.copyId); }, kLoadTimeoutSeconds);

    action ("La copia tiene el mismo estado, ganancia, mute y rutas", [this, &panel, &s]
    {
        auto* copy = graph.findInstance (s.copyId);
        auto* original = graph.findInstance (s.dexedId);

        if (copy == nullptr || original == nullptr || copy->getPlugin() == nullptr)
            return check (false, "copia no lista");

        juce::MemoryBlock copyState;
        copy->getPlugin()->getStateInformation (copyState);
        auto ok = check (copyState == s.dexedState, "estado identico (" + juce::String ((int) copyState.getSize()) + " bytes)");
        ok = check (copy->getPlugin()->getCurrentProgram() == 5, "programa " + juce::String (copy->getPlugin()->getCurrentProgram())) && ok;
        ok = check (copy->name == original->name + " (copia)", "nombre \"" + copy->name + "\"") && ok;
        ok = check (juce::approximatelyEqual (copy->gainDb, original->gainDb) && copy->mute == original->mute,
                    "ganancia " + juce::String (copy->gainDb, 1) + " dB, mute " + (copy->mute ? "si" : "no"))
             && ok;

        const auto a = router.getRoutesForInstance (s.dexedId);
        const auto b = router.getRoutesForInstance (s.copyId);
        ok = check (a.size() == b.size() && (a.empty() || (a[0].inputDevice == b[0].inputDevice && a[0].channelMask == b[0].channelMask
                                                            && a[0].remapChannel == b[0].remapChannel)),
                    "rutas MIDI copiadas: " + panel.getRowInfo (s.copyId).port + " / " + panel.getRowInfo (s.copyId).channels + " / "
                        + panel.getRowInfo (s.copyId).mode)
             && ok;
        return ok;
    });

    //==============================================================================
    action ("Vumetros: nota en Dexed con el teclado virtual", [&panel, &keys, &s]
    {
        panel.selectInstance (s.dexedId);
        keys.noteOn (InstancePanel::kKeyboardChannel, 69, 0.9f);
        return true;
    });

    waitMs (400);

    action ("Vumetro de Dexed y de la mezcla muestran senal; la copia no", [this, &panel, &content, &s]
    {
        const auto row = panel.getRowInfo (s.dexedId);
        const auto copyRow = panel.getRowInfo (s.copyId);
        auto ok = check (row.meterDb[0] > -60.0f && row.meterDb[1] > -60.0f,
                         "Dexed L " + juce::String (row.meterDb[0], 1) + " dB, R " + juce::String (row.meterDb[1], 1) + " dB");
        ok = check (copyRow.meterDb[0] <= -60.0f, "copia en silencio (" + juce::String (copyRow.meterDb[0], 1) + " dB)") && ok;
        ok = check (content.getMasterMeterDb (0) > -60.0f, "mezcla L " + juce::String (content.getMasterMeterDb (0), 1) + " dB") && ok;
        return ok;
    });

    shot ("04-vumetro-y-copia");

    //==============================================================================
    action ("Esc (pulsacion enviada a la ventana con el foco en el teclado virtual) = panico", [this, &panel, &keys, &content]
    {
        content.setStatus ("-");
        window.toFront (true);

        // Focus on the on-screen keyboard: Esc is not one of its keys, so it reaches the window.
        for (auto* child : panel.getChildren())
            if (dynamic_cast<juce::MidiKeyboardComponent*> (child) != nullptr)
                child->grabKeyboardFocus();

        const auto handled = sendKeyToWindow (window, juce::KeyPress (juce::KeyPress::escapeKey));
        auto ok = check (handled, "tecla procesada");
        ok = check (content.getStatusText().startsWith (juce::String::fromUTF8 ("Pánico")), "estado: " + content.getStatusText()) && ok;
        ok = check (! keys.isNoteOn (InstancePanel::kKeyboardChannel, 69), "la tecla del teclado virtual se solto") && ok;
        return ok;
    });

    waitMs (300);

    action ("Esc con el foco en un combo de la fila tambien es panico", [this, &content]
    {
        content.setStatus ("-");
        juce::ComboBox* combo = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (combo == nullptr)
                    if (auto* cb = dynamic_cast<juce::ComboBox*> (child); cb != nullptr && cb->isShowing())
                        combo = cb;

                find (*child);
            }
        };
        find (content);

        if (combo != nullptr)
            combo->grabKeyboardFocus();

        const auto handled = sendKeyToWindow (window, juce::KeyPress (juce::KeyPress::escapeKey));
        return check (combo != nullptr && handled && content.getStatusText().startsWith (juce::String::fromUTF8 ("Pánico")),
                      "estado: " + content.getStatusText());
    });

    action ("Teclado real del sistema (SendInput): Esc", [this, &content]
    {
        content.setStatus ("-");
        auto* hwnd = (HWND) window.getWindowHandle();
        ::SetForegroundWindow (hwnd);
        window.grabKeyboardFocus();

        if (::GetForegroundWindow() != hwnd)
        {
            cli::print ("    (omitido: Windows no dejo poner la ventana en primer plano; la prueba con handleKeyPress cubre el ruteo)");
            return true;
        }

        return check (sendOsKeyPress (VK_ESCAPE, false), "SendInput Esc enviado");
    });

    waitMs (300);

    action ("Resultado de Esc por SendInput", [this, &content]
    {
        if (::GetForegroundWindow() != (HWND) window.getWindowHandle())
            return true;

        return check (content.getStatusText().startsWith (juce::String::fromUTF8 ("Pánico")), "estado tras Esc real: " + content.getStatusText());
    });

    //==============================================================================
    action ("Ctrl+S en un proyecto sin archivo = Guardar como", [this, &s]
    {
        s.projectFile.deleteFile();
        window.grabKeyboardFocus();
        const auto handled = sendKeyToWindow (window, juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0));
        return check (handled, "Ctrl+S procesado");
    });

    waitFor ("esperar el archivo", [this, &s] { return s.projectFile.existsAsFile() && project != nullptr && project->hasFile(); }, 10.0);

    action ("Guardado: titulo sin *, estado", [this, &content, &s]
    {
        s.savedTime = s.projectFile.getLastModificationTime();
        auto ok = check (! window.getName().endsWith ("*"), "titulo \"" + window.getName() + "\"");
        ok = check (content.getStatusText().startsWith ("Guardado"), "estado: " + content.getStatusText()) && ok;
        ok = check (content.getProjectInfo().contains ("ui-prueba"), "proyecto: " + content.getProjectInfo()) && ok;
        graph.setGainDb (s.surgeId, kTestGainDb - 1.0f); // a change -> "*"
        return ok;
    });

    waitMs (1100); // file times have a resolution of ~1 s on some file systems

    action ("Ctrl+S con cambios guarda en el mismo archivo", [this]
    {
        auto ok = check (window.getName().endsWith ("*"), "titulo con * tras un cambio: \"" + window.getName() + "\"");
        ok = check (sendKeyToWindow (window, juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0)), "Ctrl+S procesado") && ok;
        return ok;
    });

    waitFor ("esperar el guardado", [this, &s] { return ! window.getName().endsWith ("*") && s.projectFile.getLastModificationTime() > s.savedTime; }, 10.0);

    action ("Ctrl+N = proyecto nuevo", [this]
    {
        return check (sendKeyToWindow (window, juce::KeyPress ('n', juce::ModifierKeys::commandModifier, 0)), "Ctrl+N procesado");
    });

    waitFor ("esperar proyecto vacio", [this] { return graph.getInstances().empty(); }, 10.0);

    action ("Proyecto nuevo vacio", [this, &content]
    {
        return check (window.getProjectInfoText().contains (juce::String::fromUTF8 ("Sin título")), "proyecto: " + content.getProjectInfo());
    });

    action ("Ctrl+O = abrir (dialogo simulado -> ui-prueba)", [this, &s]
    {
        s.gaps.begin();
        return check (sendKeyToWindow (window, juce::KeyPress ('o', juce::ModifierKeys::commandModifier, 0)), "Ctrl+O procesado");
    });

    waitFor ("esperar que empiece la carga", [this] { return graph.getInstances().size() == 4; }, 10.0);

    waitFor ("indicador de progreso visible", [&content] { return content.isProgressVisible(); }, 5.0);

    action ("Barra de estado durante la carga", [this, &content, &panel]
    {
        int loadingRows = 0;

        for (auto& i : graph.getInstances())
            loadingRows += panel.getRowInfo (i->id).loadingIndicator ? 1 : 0;

        return check (content.getProgressText().startsWith ("Cargando"), "progreso: \"" + content.getProgressText() + "\", filas con indicador: "
                                                                             + juce::String (loadingRows));
    });

    waitFor ("esperar la carga del proyecto", [this] { return graph.getNumLoading() == 0 && project != nullptr && ! project->isLoading(); },
             kLoadTimeoutSeconds);

    action ("Proyecto reabierto", [this, &content, &s]
    {
        s.gaps.end();
        juce::StringArray names;
        for (auto& i : graph.getInstances())
            names.add (i->name + (i->isReady() ? "" : " (no lista)"));

        cli::print ("    apertura del proyecto (4 plugins): hueco maximo del hilo de mensajes " + juce::String (s.gaps.maxGapMs, 0)
                    + " ms, creacion mas lenta " + juce::String (graph.getMaxCreationMs(), 0) + " ms");
        auto ok = check (names == juce::StringArray { "Surge XT", "sforzando", "Dexed", "Dexed (copia)" }, "instancias: " + names.joinIntoString (", "));
        ok = check (content.getProjectInfo().contains ("ui-prueba") && ! window.getName().endsWith ("*"), "proyecto: " + content.getProjectInfo()) && ok;
        ok = check (! content.isProgressVisible(), "indicador de progreso oculto al terminar") && ok;

        for (auto& i : graph.getInstances())
            if (i->name == "Dexed")
                s.dexedId = i->id;

        return ok;
    });

    //==============================================================================
    action ("Plegar el teclado virtual", [&panel]
    {
        panel.setKeyboardVisible (false);
        return true;
    });

    waitMs (300);
    layoutCheck ("1024x600 teclado plegado", true);
    shot ("05-teclado-plegado");

    action ("Desplegar el teclado", [this, &panel]
    {
        panel.setKeyboardVisible (true);
        return check (panel.isKeyboardVisible(), "teclado visible");
    });

    //==============================================================================
    action ("Ventana grande 1600x1000", [this]
    {
        setOuterSize (window, 1600, 1000);
        return true;
    });

    waitMs (500);
    layoutCheck ("1600x1000", true);
    shot ("06-1600x1000");

    action ("Ventana al minimo (860x540 sin el marco)", [this]
    {
        window.setSize (860, 540); // the resize limits of MainWindow
        return true;
    });

    waitMs (500);
    layoutCheck ("minimo", false);
    shot ("07-minimo");

    action ("Escala global 1.5 (simula DPI 150 %) a 1024x600 logicos", [this]
    {
        juce::Desktop::getInstance().setGlobalScaleFactor (1.5f);
        setOuterSize (window, 1024, 600);
        return true;
    });

    waitMs (800);
    layoutCheck ("1024x600 con escala 1.5", true);
    shot ("08-escala-1.5");

    action ("Volver a escala 1 y 1024x600", [this]
    {
        juce::Desktop::getInstance().setGlobalScaleFactor (1.0f);
        setOuterSize (window, 1024, 600);
        return check (juce::approximatelyEqual (juce::Desktop::getInstance().getGlobalScaleFactor(), 1.0f), "escala 1");
    });

    waitMs (500);

    //==============================================================================
    // Plugin scan from the UI: runs on background threads with a progress window. The list is
    // emptied and the same files are scanned again (out of process), then the original
    // plugins.xml content is restored exactly.
    action ("Escaneo desde la UI: vaciar la lista y re-escanear los mismos archivos", [this, &s]
    {
        auto& list = plugins.getKnownPluginList();
        s.pluginListBackup = list.createXml();
        s.pluginTypesBefore = list.getNumTypes();

        for (auto& d : list.getTypes())
            s.scanFiles.addIfNotAlreadyThere (d.fileOrIdentifier);

        list.clear();
        window.showPluginList();

        if (auto* w = window.getPluginListWindow())
            w->scanFiles (s.scanFiles);

        s.gaps.begin();
        s.progressSeenDuringScan = false;
        return check (window.getPluginListWindow() != nullptr && window.getPluginListWindow()->isScanning(),
                      "escaneo en curso: " + juce::String (s.scanFiles.size()) + " archivos");
    });

    waitFor ("esperar el fin del escaneo", [this, &content, &s]
    {
        s.progressSeenDuringScan = s.progressSeenDuringScan || content.isProgressVisible();
        return window.getPluginListWindow() == nullptr || ! window.getPluginListWindow()->isScanning();
    }, 180.0);

    action ("Escaneo terminado sin bloquear la UI; lista restaurada", [this, &s]
    {
        s.gaps.end();
        auto& list = plugins.getKnownPluginList();
        const auto found = list.getNumTypes();
        cli::print ("    escaneo: " + juce::String (found) + " plugins encontrados (antes " + juce::String (s.pluginTypesBefore)
                    + "), ticks del timer de 5 ms: " + juce::String (s.gaps.ticks) + ", hueco maximo " + juce::String (s.gaps.maxGapMs, 0) + " ms");
        auto ok = check (found == s.pluginTypesBefore, "mismos plugins tras re-escanear");
        ok = check (s.gaps.maxGapMs < 250.0, "la UI siguio respondiendo durante el escaneo (hueco maximo " + juce::String (s.gaps.maxGapMs, 0) + " ms)") && ok;
        ok = check (s.progressSeenDuringScan, "indicador \"Escaneando plugins...\" en la barra de estado") && ok;

        if (s.pluginListBackup != nullptr)
            list.recreateFromXml (*s.pluginListBackup);

        plugins.save();

        if (auto* w = window.getPluginListWindow())
            w->closeButtonPressed();

        return ok;
    });

    //==============================================================================
    action ("Volver a un proyecto vacio", [this]
    {
        window.fileChooserOverride = nullptr;
        project->newProject();
        return check (graph.getInstances().empty(), "sin instancias");
    });
}

} // namespace vstplayer
