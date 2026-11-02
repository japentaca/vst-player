#include "ui/RecordingSettingsWindow.h"

#include "ui/Theme.h"

namespace vstplayer
{

namespace
{
constexpr int kRadioGroup = 4711;
}

RecordingSettingsComponent::RecordingSettingsComponent (RecordingSettings initial,
                                                        std::function<juce::File (const RecordingSettings&)> resolve)
    : settings (std::move (initial)), resolveFolder (std::move (resolve))
{
    formatLabel.setText ("Formato:", juce::dontSendNotification);
    addAndMakeVisible (formatLabel);

    const auto formats = RecordingFormat::all();

    for (int i = 0; i < formats.size(); ++i)
        formatBox.addItem (formats[i].getLabel(), i + 1);

    formatBox.setSelectedItemIndex (juce::jmax (0, formats.indexOf (settings.format)), juce::dontSendNotification);
    formatBox.onChange = [this]
    {
        settings.format = RecordingFormat::all()[juce::jmax (0, formatBox.getSelectedItemIndex())];
        changed();
    };
    addAndMakeVisible (formatBox);

    keepWavToggle.setButtonText (juce::String::fromUTF8 ("Guardar también el WAV (32 bits float, sin pérdida)"));
    keepWavToggle.setToggleState (settings.keepWav, juce::dontSendNotification);
    keepWavToggle.onClick = [this]
    {
        settings.keepWav = keepWavToggle.getToggleState();
        changed();
    };
    addAndMakeVisible (keepWavToggle);

    folderLabel.setText ("Carpeta:", juce::dontSendNotification);
    addAndMakeVisible (folderLabel);

    projectFolderToggle.setButtonText (juce::String::fromUTF8 ("La del proyecto (donde está el .vstproj.json)"));
    fixedFolderToggle.setButtonText ("Siempre esta:");
    projectFolderToggle.setRadioGroupId (kRadioGroup);
    fixedFolderToggle.setRadioGroupId (kRadioGroup);
    projectFolderToggle.setToggleState (settings.folder.isEmpty(), juce::dontSendNotification);
    fixedFolderToggle.setToggleState (settings.folder.isNotEmpty(), juce::dontSendNotification);
    projectFolderToggle.onClick = [this]
    {
        if (projectFolderToggle.getToggleState() && settings.folder.isNotEmpty())
        {
            settings.folder.clear();
            changed();
        }
    };
    fixedFolderToggle.onClick = [this]
    {
        if (fixedFolderToggle.getToggleState() && settings.folder.isEmpty())
            chooseFolder();
    };
    addAndMakeVisible (projectFolderToggle);
    addAndMakeVisible (fixedFolderToggle);

    fixedFolderLabel.setColour (juce::Label::textColourId, theme::textDim);
    fixedFolderLabel.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (fixedFolderLabel);

    chooseFolderButton.setButtonText (juce::String::fromUTF8 ("Elegir…"));
    chooseFolderButton.onClick = [this] { chooseFolder(); };
    addAndMakeVisible (chooseFolderButton);

    openFolderButton.setButtonText ("Abrir carpeta");
    openFolderButton.setTooltip ("Abre en el Explorador la carpeta donde se va a grabar");
    openFolderButton.onClick = [this]
    {
        const auto folder = resolveFolder (settings);
        folder.createDirectory();
        folder.startAsProcess();
    };
    addAndMakeVisible (openFolderButton);

    targetLabel.setColour (juce::Label::textColourId, theme::accent);
    targetLabel.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (targetLabel);

    noteLabel.setColour (juce::Label::textColourId, theme::textDim);
    noteLabel.setFont (juce::FontOptions (13.0f));
    noteLabel.setJustificationType (juce::Justification::topLeft);
    noteLabel.setText (juce::String::fromUTF8 ("Se graba lo que suena por la salida de audio (la mezcla), a la frecuencia del dispositivo. "
                                               "El MP3 se codifica con LAME a máxima calidad mientras se graba. "
                                               "Grabar / detener: botón ● Grabar o Ctrl+R. Proyecto sin guardar: "
                                               "Documentos\\VstPlayer\\Grabaciones."),
                       juce::dontSendNotification);
    addAndMakeVisible (noteLabel);

    setSize (640, 262);
    refresh();
}

RecordingSettingsComponent::~RecordingSettingsComponent() = default;

void RecordingSettingsComponent::changed()
{
    refresh();

    if (onChange)
        onChange (settings);
}

void RecordingSettingsComponent::refresh()
{
    keepWavToggle.setEnabled (settings.format.isMp3());
    keepWavToggle.setToggleState (settings.format.isMp3() ? settings.keepWav : true, juce::dontSendNotification);
    projectFolderToggle.setToggleState (settings.folder.isEmpty(), juce::dontSendNotification);
    fixedFolderToggle.setToggleState (settings.folder.isNotEmpty(), juce::dontSendNotification);
    fixedFolderLabel.setText (settings.folder.isNotEmpty() ? settings.folder : juce::String ("(ninguna)"), juce::dontSendNotification);
    fixedFolderLabel.setTooltip (settings.folder);

    const auto folder = resolveFolder (settings);
    const auto ext = settings.format.isMp3() ? juce::String (".mp3") : juce::String (".wav");
    targetLabel.setText (juce::String::fromUTF8 ("Próxima grabación: ") + folder.getChildFile ("<proyecto> AAAA-MM-DD HH-MM-SS" + ext).getFullPathName(),
                         juce::dontSendNotification);
    targetLabel.setTooltip (targetLabel.getText());
}

void RecordingSettingsComponent::chooseFolder()
{
    const auto start = settings.folder.isNotEmpty() ? juce::File (settings.folder) : resolveFolder (settings);
    chooser = std::make_unique<juce::FileChooser> ("Carpeta para las grabaciones", start);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safeThis = juce::Component::SafePointer<RecordingSettingsComponent> (this)] (const juce::FileChooser& fc)
                          {
                              if (safeThis == nullptr)
                                  return;

                              const auto result = fc.getResult();

                              if (result != juce::File() && result.isDirectory())
                              {
                                  safeThis->settings.folder = result.getFullPathName();
                                  safeThis->changed();
                              }
                              else
                              {
                                  safeThis->refresh(); // cancelled: back to the previous choice
                              }
                          });
}

void RecordingSettingsComponent::resized()
{
    auto area = getLocalBounds().reduced (14, 12);
    constexpr int rowHeight = 28;
    constexpr int labelWidth = 76;

    auto row = area.removeFromTop (rowHeight);
    formatLabel.setBounds (row.removeFromLeft (labelWidth));
    formatBox.setBounds (row);
    area.removeFromTop (6);

    row = area.removeFromTop (rowHeight);
    row.removeFromLeft (labelWidth);
    keepWavToggle.setBounds (row);
    area.removeFromTop (10);

    row = area.removeFromTop (rowHeight);
    folderLabel.setBounds (row.removeFromLeft (labelWidth));
    projectFolderToggle.setBounds (row);
    area.removeFromTop (4);

    row = area.removeFromTop (rowHeight);
    row.removeFromLeft (labelWidth);
    fixedFolderToggle.setBounds (row.removeFromLeft (124));
    chooseFolderButton.setBounds (row.removeFromRight (80));
    row.removeFromRight (6);
    fixedFolderLabel.setBounds (row);
    area.removeFromTop (10);

    row = area.removeFromTop (rowHeight);
    openFolderButton.setBounds (row.removeFromRight (110));
    row.removeFromRight (6);
    targetLabel.setBounds (row);
    area.removeFromTop (8);

    noteLabel.setBounds (area);
}

} // namespace vstplayer
