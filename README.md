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
| ONNX Runtime + DirectML | 1.20.1 / 1.15.4 | `D:\Tools\onnxruntime` (otra ruta: `-DONNXRUNTIME_DIR=...`) |
| Modelo Spleeter 2 stems int8 | sherpa-onnx | `D:\Tools\models\spleeter-2stems-int8` (se copia junto al programa) |
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
    Automix.*           automix de dos decks: lista, plan de cada transicion y mezcla
    MixPoints.*         puntos de automix de una cancion (sonido, fundido, parte ritmica)
    StemSeparator.*     separacion de voz con IA (Spleeter en ONNX Runtime)
    StemsService.*      cola de separacion en segundo plano y cache de stems
    SampleData.*        sonidos del sampler: audio y .vdjsample de VirtualDJ
  Video/                video
    MediaFoundation.*   audio y video de MP4/MOV/WMV/M4A con Media Foundation de Windows
    DeckVideo.*         hilo de video de un deck: el fotograma que toca segun su posicion
    Compositor.*        compositor en la GPU (Direct3D 11) a ritmo fijo y sus salidas (como OBS)
    VideoMix.*          capas de cada fotograma: los decks segun el crossfader, rotulos y logo
    Scenes.*            escenas y elementos (como OBS): fuentes, colocacion, recorte y XML
    VideoProgram.*      la escena al aire, sus capas y las transiciones entre escenas
    Capture.*           captura de pantalla (DXGI) y de ventana (Windows Graphics Capture) en la GPU
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
    VideoWindow.*       vista previa y proyectores a pantalla completa (uno por monitor)
    ScenePanel.*        panel de escenas, fuentes, transicion y propiedades
    DarkLookAndFeel.*   estilo visual oscuro
Tests/
  TestMain.cpp          pruebas automáticas del motor (ctest) y herramientas:
                        MiDJTests --analyse canciones...      BPM y primer pulso
                        MiDJTests --stretch cancion 1.08 dir  keylock con una canción real
                        MiDJTests --compare a.wav b.wav       tono de b respecto de a
                        MiDJTests --sample archivo            datos de un sonido (.vdjsample o audio)
                        MiDJTests --mixpoints canciones...    puntos de automix
                        MiDJTests --stems cancion [cpu|gpu] [s] separa voz e instrumental (WAV)
                        MiDJTests --video archivo             coste de decodificar un video en la GPU y en la CPU
                        MiDJTests --benchmark                 CPU del keylock y de los efectos
```

## Automix

En el navegador, clic derecho en una canción > **Agregar a automix**. La lista aparece como **Automix** a la
izquierda, con su barra: AUTOMIX (encender), Mezclar ya, Saltar, Quitar, Vaciar y la duración del fundido.
Cada transición se planifica como el modo Smart de VirtualDJ: **a tempo** (16 pulsos, compases alineados y el
tempo deslizándose de una canción a la otra) si la actual tiene fin de parte rítmica, la siguiente tiene inicio
y sus BPM difieren menos de un 6 %; si no, **fundido**. La lista se guarda al cerrar.

## Stems (voz / instrumental)

Botón **STEMS** de cada deck: separa la voz con IA en segundo plano (en este equipo tarda algo más que la
canción) y después aparecen **VOZ** e **INSTR** para apagar cada parte. También desde el navegador: clic derecho >
**Preparar stems**. La voz se guarda en `%APPDATA%\MiDJ\Stems` (FLAC) y la próxima vez se lee de ahí en segundos.
Por defecto se usa la CPU; la tarjeta gráfica (DirectML) se activa escribiendo `<STEMS device="gpu"/>` en
`%APPDATA%\MiDJ\stems.xml` (en la Radeon R5 de este portátil DirectML se cuelga, por eso no se prueba sola).

## Video

Los MP4, MOV, M4V, WMV y 3GP se cargan en un deck como cualquier canción (también M4A, AAC y WMA), con los
codecs de Windows (Media Foundation, sin FFmpeg). Botón **VIDEO** (junto a MIDI): abre la vista previa, que
mezcla los dos decks según el crossfader; una canción sin video se ve como un rótulo con el título y el
artista. El video sigue la posición que se oye del deck: pitch, saltos, CUE y bucles.

Como en OBS, un compositor en la tarjeta gráfica (Direct3D 11) arma la imagen una vez por fotograma a ritmo
fijo (1280x720 a 30 fps) y la dibuja en cada salida; el fundido entre decks se hace en luz lineal (a la mitad
no oscurece). Salidas:
- **Vista previa** (ventana VIDEO), para el DJ.
- **Proyectores a pantalla completa**, uno por monitor, sin bordes ni cursor: doble clic en la vista previa
  abre o cierra el del segundo monitor; clic derecho, el menú con todas las pantallas y el estado del
  compositor (tarjeta, ms por fotograma, fotogramas perdidos). En el proyector: Esc o doble clic lo cierra.

Solo se compone y se decodifica mientras hay alguna de estas ventanas abierta. El video se decodifica en la
tarjeta gráfica (DXVA) y, si falla, en la CPU. Ajustes en `%APPDATA%\MiDJ\video.xml`:
`<VIDEO device="cpu" width="1280" height="720" fps="30"/>` (`device="cpu"` fuerza la CPU; el resto, el
tamaño y el ritmo del programa). En este portátil (A8 + Radeon R5, Release) un video en un deck cuesta ~34 %
de un núcleo con la vista previa y ~5 % más con un proyector; componer, ~1,2 ms por fotograma.

### Escenas

A la derecha de la vista previa está el panel de escenas, como el de OBS (se guarda en
`%APPDATA%\MiDJ\scenes.xml`; por defecto, una escena "Mezcla" con la mezcla de los decks):
- **Escenas**: clic para ponerla al aire con la transición elegida (Corte, Fundido, Fundido a negro,
  Deslizar o Barrido, de 0 a 3 s). Botones para crear, borrar y renombrar.
- **Fuentes** de la escena (la de arriba se ve encima): Mezcla de decks, Deck A, Deck B, Imagen (PNG, JPG,
  GIF), Texto, Color, Captura de pantalla y Captura de ventana. En el texto, `{titulo}`, `{artista}` y
  `{bpm}` se cambian por la canción del deck que más se ve (por ejemplo "Ahora suena: {titulo}"). Clic en
  el punto: mostrar u ocultar; Subir y Bajar.
- **Propiedades** de la fuente elegida: texto, tamaño, negrita, sombra, color, imagen, opacidad, giro,
  recorte, Ajustar a pantalla y Restablecer.
- En la vista previa: clic para elegir una fuente (recuadro rojo), arrastrar para moverla (se pega a los
  bordes y al centro; Mayús para no pegarse), arrastrar una esquina para escalarla sin deformar (Mayús para
  deformar), flechas para mover de 1 en 1 (Mayús: de 10 en 10) y Supr para quitarla.

### Captura de pantalla y de ventana

Como en OBS, la imagen capturada no sale de la tarjeta gráfica (se copia de textura a textura):
- **Captura de pantalla**: se elige el monitor (por defecto, el principal, encajado en el programa). Usa la
  duplicación del escritorio de DXGI, que en Windows 10 no pinta el borde amarillo, y dibuja encima el
  puntero del ratón (se puede quitar). Si la pantalla es de otra tarjeta gráfica, usa Windows Graphics Capture.
- **Captura de ventana**: se elige de la lista de ventanas abiertas (`[programa] título`, botón Actualizar).
  Usa Windows Graphics Capture (Windows 10 1903+): captura la ventana aunque esté tapada; con "Solo el
  contenido" quita la barra de título y los bordes. En Windows 10 la ventana capturada lleva un borde
  amarillo (lo pone Windows; en Windows 11 se quita). Se guarda el título, la clase y el programa: al abrir
  MiDJ se busca la misma ventana (mismo título, si no la misma clase, si no el mismo programa), y si se
  cierra y se vuelve a abrir, la captura vuelve sola.
- Debajo se ve el estado: tamaño capturado y método (DXGI o WGC), o por qué no captura. Dos fuentes que
  capturan lo mismo comparten la captura.

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
SQLite es de dominio público. ONNX Runtime es MIT; DirectML.dll es redistribuible según su licencia de Microsoft;
los pesos de Spleeter son de Deezer con licencia MIT (exportados a ONNX por el proyecto sherpa-onnx, Apache 2.0).

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
- Automix de dos decks con puntos de automix calculados al cargar cada canción (sonido, fundido y parte rítmica, con las reglas de VirtualDJ): mezcla a tempo o con fundido, mezclar ya, saltar, lista guardada y acciones MIDI.
- Stems con IA: voz e instrumental con Spleeter 2 stems (ONNX Runtime, CPU o DirectML) separados en segundo plano con caché en disco; mezcla en el deck sin clics y exacta con los dos encendidos; botones VOZ/INSTR, preparar desde el navegador y acciones MIDI.
- Video: MP4/MOV/WMV (y M4A/AAC/WMA) en los decks con Media Foundation de Windows; un hilo de video por deck que sigue la posición que se oye (tras un salto, primer fotograma en ~0,1 s y alcanza al audio sin congelarse); ventana VIDEO con la mezcla según el crossfader, rótulo con título y artista para las canciones sin video y pantalla completa sin bordes en el segundo monitor.
- Video en la GPU: decodificación DXVA con Direct3D 11 (8 veces menos CPU en este equipo; saltos en ~45 ms) con vuelta a la CPU si falla, y sin decodificar mientras la ventana de video está cerrada.
- Compositor de video en la GPU como el de OBS (Direct3D 11, 1280x720 a 30 fps, fundido en luz lineal) con vista previa y proyectores a pantalla completa en cualquier monitor, sin bordes ni cursor; sin copias de fotogramas entre el decodificador y la GPU más allá de la subida.
- Escenas como en OBS: fuentes (mezcla de decks, deck A/B, imagen, texto con título/artista/BPM de la canción que suena, color) con posición, escala, giro, recorte y opacidad; edición con el ratón en la vista previa; transiciones Corte, Fundido, Fundido a negro, Deslizar y Barrido compuestas en la GPU; guardadas en `scenes.xml`.
- Captura de pantalla (duplicación de DXGI, con el puntero del ratón y sin el borde amarillo de Windows 10) y de ventana (Windows Graphics Capture, solo el contenido o con bordes) como fuentes de escena, sin salir de la GPU; la ventana se vuelve a encontrar al reabrir MiDJ o al reabrirla.
