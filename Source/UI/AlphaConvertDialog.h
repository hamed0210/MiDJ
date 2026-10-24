#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace midj
{
// Al elegir un video que puede tener transparencia (.webm, .mov, .mkv) para una fuente Multimedia o
// un stinger: si la tiene, se convierte con FFmpeg a un MP4 con mate (ventana con avance y
// Cancelar) y se usa ese. Sin FFmpeg en el equipo, ofrece descargarlo. El convertido se guarda
// junto al original (<nombre>_mate.mp4) y la proxima vez se usa directamente.
namespace AlphaConvertDialog
{
    // El tamano del programa: cada mitad del convertido cabe en el (sin agrandar).
    void setProgramSize (int width, int height);

    // done (en el hilo de la interfaz): el archivo a usar y si es un video con mate convertido.
    // Si no hay que convertir (no tiene transparencia, se cancela o falla), el elegido y false.
    void prepare (const juce::File& chosen, std::function<void (const juce::File& file, bool matte)> done);
}
}
