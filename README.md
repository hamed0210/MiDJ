# MiDJ

Aplicación de DJ para Windows escrita en C++20 con [JUCE](https://juce.com).
Primer objetivo: dos decks que reproducen MP3/WAV/FLAC/Ogg, BPM detectado, control de tempo y crossfader.

## Requisitos

| Herramienta | Versión | Dónde está en este equipo |
|---|---|---|
| Visual Studio 2022 Build Tools (C++) | 17.x | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools` |
| CMake | 3.22 o superior | `D:\Tools\cmake\bin` |
| JUCE | 8.0.4 | `D:\Tools\JUCE` (otra ruta: `-DJUCE_DIR=...`) |
| Rubber Band | 4.0.0 | `D:\Tools\rubberband` (otra ruta: `-DRUBBERBAND_DIR=...`) |
| SQLite (amalgamation) | 3.46.1 | `D:\Tools\sqlite` (otra ruta: `-DSQLITE_DIR=...`) |
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
    ChannelStrip.*      canal de un deck: ganancia, EQ de 3 bandas, filtro, volumen
    BpmDetector.*       detección de tempo y del primer pulso
    TimeStretcher.*     keylock: cambio de tempo sin cambio de tono (Rubber Band)
    Effects.*           efectos sincronizados al tempo y rack de 3 ranuras por deck
    Sampler.*           sampler de 8 pads: disparo, tempo, grabacion y banco
    SampleData.*        sonidos del sampler: audio y .vdjsample de VirtualDJ
  Control/              controladoras MIDI
    Actions.*           acciones asignables (play, cue, tempo, EQ, efectos, jog...)
    MidiMapping.*       asignaciones mensaje MIDI -> accion, guardadas en XML
    MidiController.*    dispositivos, modo aprender y traduccion de mensajes
    ControlSurface.*    ejecuta las acciones sobre decks, mezclador y efectos
  Library/              biblioteca de canciones
    Library.*           base SQLite, carpetas, escaneo, analisis de fondo y busqueda
    TagReader.*         etiquetas ID3 (v1, v2.2-2.4) y 'Artista - Titulo' del nombre
  UI/                   interfaz
    DeckComponent.*     panel de un deck: forma de onda, botones, tempo
    MixerComponent.*    columna central del mezclador: mandos y faders de volumen
    LibraryBrowser.*    navegador: carpetas, buscador y lista (arrastrar o doble clic)
    EffectsRow.*        fila FX de un deck: elegir efecto, encender y mando
    MidiLearnOverlay.*  capa del modo aprender MIDI
    SamplerPanel.*      panel del sampler: pads, volumen y STOP
    DarkLookAndFeel.*   estilo visual oscuro
Tests/
  TestMain.cpp          pruebas automáticas del motor (ctest) y herramientas:
                        MiDJTests --analyse canciones...      BPM y primer pulso
                        MiDJTests --stretch cancion 1.08 dir  keylock con una canción real
                        MiDJTests --compare a.wav b.wav       tono de b respecto de a
                        MiDJTests --sample archivo            datos de un sonido (.vdjsample o audio)
                        MiDJTests --benchmark                 CPU del keylock y de los efectos
```

## Sampler

8 pads a la derecha del navegador. Clic en un pad vacío o arrastrar encima un archivo (o una canción del
navegador) para cargarlo; acepta audio y los `.vdjsample` de VirtualDJ con su tempo y bucle. Clic: disparar.
Clic derecho: grabar 4, 8 o 16 pulsos de un deck (empieza en el siguiente pulso y queda como bucle bloqueado),
modo de disparo (on/off, mantener, repetir), bucle, tempo (libre, sincronizado, bloqueado) y quitar.
El banco se guarda en `%APPDATA%\MiDJ\sampler.xml` y las grabaciones en `%APPDATA%\MiDJ\Grabaciones`.

## Controladoras MIDI

Botón **MIDI** (junto al crossfader): clic en un control de la pantalla y después moverlo en la
controladora; queda asignado (verde). Clic derecho quita la asignación. Se guarda en
`%APPDATA%\MiDJ\midi.xml` y las controladoras que se conecten con la app abierta se detectan solas.

Sin controladora se puede probar con `MiDJ.exe --simular-midi archivo.txt`. Cada línea del archivo:

```text
# milisegundos  tipo(note/cc/pb)  canal  número  valor
20000 note 1 36 127
30000 cc 1 7 100
40000 pb 1 0 8192
```

## Licencias

Rubber Band es GPL. Para uso propio no hay problema; para vender MiDJ hace falta su licencia
comercial o cambiarla por otra librería (por ejemplo SoundTouch, LGPL). Solo `TimeStretcher.*` la usa.
SQLite es de dominio público.

## Reglas del motor de audio

- El hilo de audio (`getNextAudioBlock`) nunca reserva memoria, bloquea, lee disco ni toca la interfaz.
- Lo lento (leer el archivo, analizar el BPM) va en hilos aparte.
- Todo cambio de volumen se aplica con rampa dentro del bloque para evitar clics.

## Registro de cambios

- Esqueleto: dos decks, crossfader con curva smooth y detección de BPM.
- SYNC, CUE y bucles de 1-2-4-8 pulsos.
- Mezclador por deck: ganancia (hasta +12 dB), EQ de 3 bandas con kill (cruces en 200 y 6500 Hz), filtro de un mando (paso bajo / paso alto con zona muerta) y fader de volumen.
- Keylock (botón KEY, activado por defecto): cambio de tempo sin cambiar el tono con Rubber Band R2. Detector de BPM más fiable con música real: ataques de mezcla y graves en escala logarítmica y tempo elegido con un peine de 1-4 pulsos (corrige el 4/3 del reggaeton).
- Biblioteca en SQLite (`%APPDATA%\MiDJ\library.db`) y navegador: carpetas, escaneo y análisis de BPM en segundo plano, búsqueda sin acentos ni mayúsculas, columnas ordenables; cargar arrastrando a un deck (también desde el Explorador) o con doble clic. Las canciones ya analizadas cargan sin volver a analizar.
- Efectos: 3 ranuras por deck con Echo (medio pulso, cola al apagar), Reverb, Flanger (barrido de 8 pulsos), BitCrush (fórmula de VirtualDJ) y Gate (cuartos de pulso), todos sincronizados al BPM del deck, sin clics al encender, apagar o cambiar, y sin tocar la señal cuando están apagados.
- Controladoras MIDI: modo aprender en la pantalla, botones por nota o CC, faders por CC o pitch bend, jog y encoders relativos (detecta complemento a dos o desplazado), los controles de la pantalla siguen a la controladora, conexión en caliente y modo de simulación.
- Sampler de 8 pads: disparo on/off, mantener y repetir; tempo libre, sincronizado (plegado a 0,66-1,5) o bloqueado al pulso; fundidos de 128 muestras; grabación de 4/8/16 pulsos de un deck empezando en el pulso; lee los .vdjsample de VirtualDJ; banco guardado; pads asignables por MIDI.
