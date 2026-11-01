#include "app/Translations.h"

#include <juce_core/juce_core.h>

namespace vstplayer
{

namespace
{
// LocalisedStrings format: "original" = "translation"
constexpr const char* kSpanish = R"TXT(language: Spanish
countries: es ar mx

"(no audio input channels found)" = "(no se encontraron canales de entrada)"
"(no audio output channels found)" = "(no se encontraron canales de salida)"
"Active MIDI inputs:" = "Entradas MIDI activas:"
"Active input channels:" = "Canales de entrada activos:"
"Active output channels:" = "Canales de salida activos:"
"Add a folder..." = "Agregar una carpeta..."
"Audio buffer size:" = "Tamaño de buffer:"
"Audio device type:" = "Tipo de dispositivo:"
"Cancel" = "Cancelar"
"Category" = "Categoría"
"Change folder..." = "Cambiar carpeta..."
"Clear list" = "Vaciar la lista"
"Control Panel" = "Panel de control"
"Deactivated after failing to initialise correctly" = "Desactivado tras fallar al inicializar"
"Description" = "Descripción"
"Device:" = "Dispositivo:"
"Error when trying to open audio device!" = "¡Error al abrir el dispositivo de audio!"
"Format" = "Formato"
"Input:" = "Entrada:"
"MIDI Output:" = "Salida MIDI:"
"Manufacturer" = "Fabricante"
"Name" = "Nombre"
"No MIDI inputs available" = "No hay entradas MIDI"
"OK" = "Aceptar"
"Opens the device's own control panel" = "Abre el panel de control propio del dispositivo"
"Options..." = "Opciones..."
"Output:" = "Salida:"
"Plays a test tone" = "Reproduce un tono de prueba"
"Plugin Scanning" = "Escaneo de plugins"
"Remove all XFMTX plug-ins" = "Quitar todos los plugins XFMTX"
"Remove any plug-ins whose files no longer exist" = "Quitar los plugins cuyos archivos ya no existen"
"Remove plug-in from list" = "Quitar el plugin de la lista"
"Remove selected plug-in from list" = "Quitar el plugin seleccionado de la lista"
"Reset Device" = "Reiniciar dispositivo"
"Sample rate:" = "Frecuencia de muestreo:"
"Scan complete" = "Escaneo terminado"
"Scan for new or updated XFMTX plug-ins" = "Buscar plugins XFMTX nuevos o actualizados"
"Scan" = "Escanear"
"Scanning for plug-ins..." = "Escaneando plugins..."
"Searching for all possible plug-in files..." = "Buscando archivos de plugins..."
"Select folders to scan..." = "Elegí las carpetas a escanear..."
"Show advanced settings..." = "Opciones avanzadas..."
"Show folder containing plug-in" = "Mostrar la carpeta del plugin"
"Show folder containing selected plug-in" = "Mostrar la carpeta del plugin seleccionado"
"Test" = "Probar"
"Testing" = "Probando"
"The following files appeared to be plugin files, but failed to load correctly" = "Estos archivos parecían plugins pero no cargaron bien"
"The following files encountered fatal errors during validation" = "Estos archivos fallaron gravemente al validarse (quedan desactivados)"
"change..." = "cambiar..."
"none" = "ninguno"
)TXT";
} // namespace

void installSpanishTranslations()
{
    juce::LocalisedStrings::setCurrentMappings (new juce::LocalisedStrings (juce::String::fromUTF8 (kSpanish), false));
}

} // namespace vstplayer
