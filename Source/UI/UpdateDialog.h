#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace midj
{
// Buscar actualizaciones en GitHub (ver App/Updates.h). Si hay una version nueva, un aviso con
// sus novedades y Descargar e instalar (baja el instalador con su avance, lo abre y cierra MiDJ:
// el instalador solo pide "Actualizar"), Omitir esta version o Mas tarde.
namespace UpdateDialog
{
    // Al abrir MiDJ: si esta activado y no se miro en las ultimas 20 h, mira en segundo plano y
    // solo avisa si hay una version nueva que no se haya omitido. Sin conexion no dice nada.
    void checkOnStartup (juce::Component& parent);
    // Ajustes > Buscar actualizaciones: avisa siempre (nueva, al dia o por que no se pudo).
    void checkNow (juce::Component& parent);

    bool getCheckOnStartup();
    void setCheckOnStartup (bool on);
    juce::String currentVersion();
}
}
