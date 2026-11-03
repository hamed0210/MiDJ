# MiDJ — desarrollo

Cómo compilar MiDJ, cómo está organizado el código y qué ha cambiado en cada versión.

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
| WebView2 SDK (cargador estático) | 1.0.3240.44 | `D:\Tools\WebView2` (otra ruta: `-DWEBVIEW2_DIR=...`) |
| Inno Setup (solo para el instalador) | 6.7 | `%LOCALAPPDATA%\Programs\Inno Setup 6` |
| FFmpeg (solo para convertir videos con transparencia) | build de BtbN, win64 gpl | `D:\Tools\ffmpeg` |
| Git | cualquiera | ya instalado |

Para usar MiDJ hace falta Windows 10 1903 o posterior. YouTube usa además el motor WebView2 de Microsoft,
que traen Windows 11 y casi todos los Windows 10 (si falta, el instalador lo avisa).

## Compilar y ejecutar

Desde una terminal en esta carpeta:

```bat
D:\Tools\cmake\bin\cmake --preset vs2022
D:\Tools\cmake\bin\cmake --build --preset debug
D:\Tools\cmake\bin\ctest --preset debug
build\MiDJ_artefacts\Debug\MiDJ.exe
```

Para una versión optimizada usa `--preset release` en la compilación.

### Instalador

Con la versión Release compilada, el instalador se arma con Inno Setup:

```bat
"%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" Installer\MiDJ.iss
```

Deja `build\installer\MiDJ-Setup-<versión>.exe`. Instala para el usuario (sin administrador) o para todos;
lleva el programa, ONNX Runtime y DirectML, el modelo de stems y las DLL de Visual C++ (no hace falta el
paquete de Microsoft), accesos directos y desinstalador. Los datos de `%APPDATA%\MiDJ` (biblioteca, escenas,
ajustes) no se tocan ni al instalar ni al desinstalar.

**Actualizar:** si MiDJ ya está instalado, el instalador lo detecta y muestra solo una página, *Actualizar
MiDJ 0.4.0 -> 0.5.0*: usa la misma carpeta, el mismo modo (usuario o todos) y los mismos accesos directos, y
cierra MiDJ si está abierto. Con la misma versión ofrece *Reinstalar*; si lo instalado es más nuevo, avisa antes
de volver a la anterior. Para cada versión nueva basta con cambiar `VERSION` en `CMakeLists.txt` (de ahí la
toma el programa) y `AppVersion` en `Installer\MiDJ.iss`.

### Publicar una versión (y que MiDJ la encuentre)

MiDJ busca actualizaciones en la API de GitHub (`/repos/hamed0210/MiDJ/releases/latest`, ver
`Source/App/Updates.*`). Para que una versión le llegue a todos:

1. Subir la versión (arriba), compilar Release, pasar `ctest` y armar el instalador.
2. En GitHub > **Releases** > **Draft a new release**: etiqueta **`vX.Y.Z`** (con la `v`), título `MiDJ X.Y.Z`,
   las novedades en Markdown (empezar con *## Novedades*: el aviso de MiDJ muestra desde ahí) y adjuntar
   **`MiDJ-Setup-X.Y.Z.exe`** (el nombre debe empezar por `MiDJ-Setup` y terminar en `.exe`).
3. Publicarla sin marcar *pre-release* (las previas no se ofrecen).

Los MiDJ abiertos la ven al arrancar (una vez al día como mucho) o con *Ajustes > Buscar ahora*: el aviso
muestra las novedades y **Descargar e instalar** baja el instalador, lo abre y cierra MiDJ. El enlace
<https://github.com/hamed0210/MiDJ/releases/latest> del README siempre apunta a la última.

### Logo

El logo (un disco de vinilo con su brazo) está en `Resources\Logo`: `icon.svg` e `icon-small.svg` (hasta
32 px) son los originales; el icono del programa, `MiDJ.ico` del instalador, las imágenes del asistente y
`logo.png` se generan con Edge a partir de ellos:

```bat
powershell -ExecutionPolicy Bypass -File Resources\Logo\render.ps1
```

Dentro de la aplicación (barra de arriba y fondo de la salida de video) se dibuja con `Source\Brand\Logo.*`.

## Estructura

```
Source/
  Main.cpp              arranque de la aplicación y ventana
  MainComponent.*       ventana principal: une motor e interfaz
  App/Updates.*         actualizaciones: ultima version en GitHub, comparar versiones y ajustes
  Remote/RemoteServer.*  control remoto: servidor HTTP y WebSocket en la red local (pagina, clave, celulares)
  ThirdParty/qrcodegen/ generador de codigos QR (Project Nayuki, MIT)
  Brand/Logo.*          el logo dibujado (disco y nombre)
  Engine/               motor de audio (sin interfaz, se puede probar solo)
    Deck.*              un plato: carga, reproduce, velocidad, BPM
    Mixer.*             mezcla de dos decks con crossfader
    MasterTap.*         lo que sale por el master y su pulso, para el visualizador
    AuxAudio.*          sonidos que no son de los decks al master (multimedia, capturadora)
    HeadphoneOutput.*   audifonos por otra tarjeta de sonido (cola con correccion de reloj)
    ChannelStrip.*      canal de un deck: ganancia, EQ de 3 bandas, filtro, volumen
    AudioFormats.*      formatos de audio: WAV, AIFF, FLAC, Ogg y MP3 (con la duración bien en los MPEG-2)
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
    Visualizer.*        visualizador del master: espectro, onda y pulso
    MediaSources.*      fuentes Multimedia (archivo) y Dispositivo de captura de video
    YouTubeSearch.*     busqueda en YouTube sin clave (lee la pagina de resultados)
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
    MixerComponent.*    centro: volumenes con pre-escucha, AUDIO (EQ, master, audifonos) / PROGRAMA, crossfader y transiciones
    LibraryBrowser.*    navegador: carpetas, favoritos, listas, YouTube, buscador y lista
    AutomixPanel.*      panel del automix: pestanas AUTOMIX y YOUTUBE, lista, encender, mezclar ya, saltar
    UpdateDialog.*      aviso de version nueva, descarga del instalador y Buscar ahora
    RemotePanel.*       panel del control remoto: encender, codigo QR, direccion y clave
    YouTubePlayer.*     reproductor de YouTube: WebView2 en una ventana fuera de las pantallas
    YouTubePanel.*      pestana YOUTUBE: lo que suena, avance, volumen y lista del automix de YouTube
    QueueRows.h         filas de las dos listas del automix: subir, bajar, quitar y arrastrar
    EffectsRow.*        fila FX de un deck: elegir efecto, encender y mando
    MidiLearnOverlay.*  capa del modo aprender MIDI
    SamplerPanel.*      panel del sampler: pads, volumen y STOP
    VideoWindow.*       escenario de video: vista previa, proyectores y edicion con el raton
    ScenePanel.*        paneles Escenas y Fuentes (como los de OBS)
    SourceDialogs.*     dialogos Anadir fuente y Propiedades (como los de OBS)
    Icons.*             iconos dibujados de botones, fuentes y tipos de archivo
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
                        MiDJTests --media-idle video [compartido]  CPU de una fuente de video abierta fuera del aire
                        MiDJTests --load-cost cancion         lo que cuesta cada paso de cargar una canción en un deck
                        MiDJTests --updates                   la ultima version publicada en GitHub (lo que ve "Buscar ahora")
                        MiDJTests --benchmark                 CPU del keylock y de los efectos
Installer/
  MiDJ.iss              instalador (Inno Setup): instala o actualiza
Resources/
  Logo/                 logo: SVG originales, render.ps1 y las imagenes que genera
  Remote/remote.html    la pagina del control remoto (va dentro de MiDJ.exe)
  Tools/alpha-a-mate.ps1  video con transparencia -> MP4 con mate (con FFmpeg)
```

## Reglas del motor de audio

- El hilo de audio (`getNextAudioBlock`) nunca reserva memoria, bloquea, lee disco ni toca la interfaz.
- Lo lento (leer el archivo, analizar el BPM) va en hilos aparte.
- Todo cambio de volumen se aplica con rampa dentro del bloque para evitar clics.

## Licencias

Rubber Band es GPL. Para uso propio no hay problema; para vender MiDJ hace falta su licencia
comercial o cambiarla por otra librería (por ejemplo SoundTouch, LGPL). Solo `TimeStretcher.*` la usa.
SQLite es de dominio público. ONNX Runtime es MIT; DirectML.dll es redistribuible según su licencia de Microsoft;
los pesos de Spleeter son de Deezer con licencia MIT (exportados a ONNX por el proyecto sherpa-onnx, Apache 2.0).
El generador de códigos QR (`Source/ThirdParty/qrcodegen`) es de Project Nayuki, con licencia MIT.

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
- Fuentes **Multimedia** (archivo de video o audio con bucle, reiniciar al activarse, no mostrar nada al terminar, cerrar si está inactiva, velocidad y su sonido al master) y **Dispositivo de captura de video** (capturadoras USB/HDMI y webcams con Media Foundation: resolución, FPS y formato, volteo, decodificación por hardware y el audio HDMI de su entrada de Windows, remuestreado con corrección de reloj), como en OBS.
- Interfaz como VirtualDJ + OBS: video integrado en la pestaña VIDEO del mezclador (vista previa, nivel de video por deck, transición), crossfader dentro del mezclador, sampler en la fila del medio, paneles inferiores ajustables a lo ancho (carpetas, canciones, automix en su propio panel, escenas y fuentes), diálogos Añadir fuente y Propiedades como los de OBS, ojo y candado por fuente, icono de audio o video en la lista, una sola forma de onda por deck y atajos de teclado (Espacio, Tab, C/S/K, Intro, flechas, A, 1-8, F1-F9, V, Ctrl+F).
- Interfaz "Escenario": estilo oscuro nuevo; decks con portada del video, pitch con el centro marcado y enganche, loops, stems, efectos y transporte; centro con pestañas AUDIO (EQ, filtro, vúmetros y volumen del master) y PROGRAMA, crossfader con centro marcado y volúmenes a los lados, sampler en fila; paneles de abajo con barritas de ancho y Ajustes para ocultarlos; biblioteca con árbol Música local / Mis carpetas / Discos, quitar con Deshacer y sincronizar con el PC; automix con iconos.
- Transiciones propias: stinger (video con punto de cambio y su sonido), barrido con imagen en grises (máscara en la GPU) y a color, con probar en el programa y modificar; guardadas en `scenes.xml`.
- Videos con mate (color a la izquierda, transparencia a la derecha) para stingers y fuentes Multimedia con transparencia, recortados en la GPU; script con FFmpeg para convertir ProRes 4444 / WebM VP9 con alfa.
- Interfaz "Escenario" v2: programa grande con los volúmenes a los lados, sampler y pestañas AUDIO / PROGRAMA en la barra de arriba, crossfader y transiciones en una línea, forma de onda delgada y partes de los decks ocultables desde Ajustes (el deck se compacta y lo de abajo crece).
- Biblioteca: favoritos (estrella), más reproducidos (30 s sonando) y listas de reproducción propias; el automix las reproduce desde "Listas".
- Escenas automáticas (cuando suena audio, video, YouTube o nada), Ctrl+1-9 para las escenas, Espacio que pausa lo que suena y vuelve a eso mismo, crossfader automático al dar Play, Escenas y Fuentes ocultables por separado con barrita entre ellas.
- Audio: tarjeta de sonido, frecuencia y bloque en Ajustes; audífonos con pre-escucha por deck (salidas 3-4, cable en Y u otra tarjeta con corrección de reloj), mezcla cue / master y volumen; limitador del master a -0,3 dBFS. La ventana recuerda su tamaño y posición.
- YouTube: búsqueda sin clave en la biblioteca, reproductor WebView2 fuera de las pantallas (página normal, H.264, anuncios incluidos), fuente de escena YouTube, escena automática y automix de YouTube con su propia lista.
- Instalador con Inno Setup (para el usuario o para todos, con las DLL de Visual C++ y aviso si falta WebView2) que actualiza una instalación anterior en su sitio.
- Logo: disco de vinilo con su brazo, como icono del programa y del instalador, en la barra de arriba y en el fondo de la salida de video.
- Automix: pausar a mano la canción que suena ya no pasa a la siguiente (al dar Play sigue); el crossfader automático también funciona con el automix encendido, salvo mientras mezcla, y el automix sigue desde el deck que se puso a sonar.
- Actualizaciones: MiDJ mira en GitHub si hay una versión nueva al abrir (una vez al día, se quita en Ajustes) o con *Ajustes > Buscar ahora*; muestra las novedades y descarga e instala el instalador. La versión del programa sale de `CMakeLists.txt`.
- Control remoto desde el celular por la red local: MiDJ sirve la página (`Resources/Remote/remote.html`) y habla con ella por WebSocket con una clave que lleva el código QR; mezcla, automix, búsqueda y carga, pads y escenas. Usa las mismas acciones que las controladoras MIDI.
- Etiquetas con la ñ y los acentos bien: el texto de 8 bits se reconoce como UTF-8, Windows-1252 o la página 850 de DOS (la ñ grabada como A4), y se repara el que otro programa ya convirtió mal ("Ma¤anita"); la biblioteca se corrige una vez al abrir. Automix con los botones más pequeños abajo (más alto para la lista), números de las carpetas separados de la barra de desplazamiento, sin "Ver en GitHub" en el aviso de actualización y búsqueda de YouTube y tipo de archivo en el control remoto.
