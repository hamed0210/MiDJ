# MiDJ

Aplicación de DJ para Windows escrita en C++20 con [JUCE](https://juce.com).
Primer objetivo: dos decks que reproducen MP3/WAV/FLAC/Ogg, BPM detectado, control de tempo y crossfader.

## Requisitos

| Herramienta | Versión | Dónde está en este equipo |
|---|---|---|
| Visual Studio 2022 Build Tools (C++) | 17.x | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools` |
| CMake | 3.22 o superior | `D:\Tools\cmake\bin` |
| JUCE | 8.0.4 | `D:\Tools\JUCE` (otra ruta: `-DJUCE_DIR=...`) |
| Git | cualquiera | ya instalado |

## Compilar y ejecutar

Desde una terminal en esta carpeta:

```bat
D:\Tools\cmake\bin\cmake --preset vs2022
D:\Tools\cmake\bin\cmake --build --preset debug
D:\Tools\cmake\bin\ctest --preset debug
build\MiDJ_artefacts\Debug\MiDJ.exe
```

Para una versión optimizada usa `--preset release` en la compilación.

## Estructura

```
Source/
  Main.cpp              arranque de la aplicación y ventana
  MainComponent.*       ventana principal: une motor e interfaz
  Engine/               motor de audio (sin interfaz, se puede probar solo)
    Deck.*              un plato: carga, reproduce, velocidad, BPM
    Mixer.*             mezcla de dos decks con crossfader
    BpmDetector.*       detección de tempo
  UI/                   interfaz
    DeckComponent.*     panel de un deck: forma de onda, botones, tempo
    DarkLookAndFeel.*   estilo visual oscuro
Tests/
  TestMain.cpp          pruebas automáticas del motor (ctest)
```

## Reglas del motor de audio

- El hilo de audio (`getNextAudioBlock`) nunca reserva memoria, bloquea, lee disco ni toca la interfaz.
- Lo lento (leer el archivo, analizar el BPM) va en hilos aparte.
- Todo cambio de volumen se aplica con rampa dentro del bloque para evitar clics.
