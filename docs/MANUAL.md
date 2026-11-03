# MiDJ — manual de uso

Todo lo que hace MiDJ, función por función. Para empezar rápido, mira el [README](../README.md).

## Interfaz y atajos de teclado

Diseño "Escenario" (oscuro, deck A en azul y B en ámbar), pensado para una pantalla de 1366x768:
- **Arriba**, una barra con el logo, el formato de la salida de video y debajo el **medidor del audio** ("audio
  31 %": lo que tarda el cálculo de cada bloque frente a lo que dura; "· N cortes" si la tarjeta se quedó sin
  audio; en rojo desde el 70 % o con cortes), el **sampler** (8 pads, volumen y Parar), las
  pestañas **AUDIO / PROGRAMA** del centro, **Proyector** (pantalla completa en un monitor), **MIDI**,
  **Atajos** y **Ajustes**.
- **Tarjeta de sonido**: si la guardada no abre, se usa la predeterminada en estéreo. Si ninguna abre, o la
  abierta no pide audio (auriculares Bluetooth apagados, HDMI sin altavoces...), los decks siguen avanzando
  en silencio con un reloj interno (arriba dice *audio sin tarjeta*) y cada 2 s se busca una que suene. Si
  cambia la salida predeterminada de Windows (se conectan altavoces, una interfaz USB...), MiDJ pasa a ella
  sin cerrarse; la elegida en Ajustes sigue guardada para el próximo arranque.
- **Decks** a los lados: portada (un fotograma si es un video), título, artista, BPM y tiempo, la forma de
  onda delgada (lo ya sonado más apagado), el **pitch** ±16 % con el centro marcado (raya y 0 en verde cuando
  está en 0; se engancha al pasar cerca; doble clic: 0) y **KEY**, bucles 1-2-4-8, stems (Voz / Instrumental
  o "Separar stems"), tres efectos (el nombre enciende, la flecha elige otro, el mando es la cantidad) y
  Play, Cue y Sync. Play mientras la canción carga ("cargando, luego suena") la pone a sonar al terminar.
- **Centro**: a los lados el **volumen** de cada deck (fader con vúmetro y, con audífonos, su botón de
  pre-escucha) y en medio la página elegida: **PROGRAMA** (la salida de video, lo más grande que quepa) o
  **AUDIO** (High, Mid, Low, Gain y Filter de cada deck con su vúmetro, el **master** con su volumen y
  vúmetro estéreo y, con audífonos, sus mandos). Debajo, en una línea, el **crossfader** con su centro
  marcado (CENTRO en verde o "40 % hacia B"; se engancha y doble clic lo centra) y la **transición** entre
  escenas (las de siempre, las propias y **+**) con su duración (botón: lista de duraciones).
- **Abajo**: biblioteca, automix y escenas/fuentes; las barritas entre ellos (y entre el árbol y la lista, y
  entre Escenas y Fuentes) cambian el tamaño (doble clic: el original).
- **Ajustes**: ocultar Biblioteca, Automix, Escenas, Fuentes, Sampler o Transiciones; el crossfader
  automático; en los decks, la forma de onda, el pitch y KEY, los loops, los stems o los efectos (el deck se
  compacta sin huecos y lo de abajo crece); **Audio y audífonos...** (ver abajo) y restablecer. Se guarda en
  `%APPDATA%\MiDJ\layout.xml`; la ventana recuerda su tamaño, posición y si estaba maximizada (`window.txt`).
- **Crossfader**: al arrancar MiDJ está del todo en A. **Automático** (con clic en Play, espacio o MIDI): al
  dar Play a un deck, si el otro no suena y el crossfader lo deja apagado, pasa a su lado en 0,4 s; si el otro
  ya sonaba, pasa del todo a su lado y el otro se pausa al llegar (un cambio de canción). Si arrancan los dos
  a la vez, o se agarra el crossfader con el ratón, no se toca nada; tampoco mientras el automix mezcla. Se
  quita en Ajustes ("Play: crossfader al deck, pausa el otro").

El **deck activo** (marco del color del deck) es el de los atajos: se elige con un clic en el deck, con
Tab o cargando una canción en él. Botón **Atajos**:

| Tecla | Acción |
|---|---|
| Espacio | Pausa lo que suena (uno o los dos decks, o YouTube); otra vez: vuelve eso mismo. Sin nada sonando: Play del deck activo |
| Tab | Cambiar de deck activo |
| C / S / K | CUE / SYNC / Keylock del deck activo |
| Intro | Cargar la canción elegida en el deck activo (en YouTube: verla) |
| Flechas arriba / abajo (RePág / AvPág) | Moverse por la lista de canciones |
| Flechas izquierda / derecha | Crossfader a pasos del 5 % (con Mayús: del todo) |
| A | Agregar la canción elegida al automix (en YouTube: al automix de YouTube) |
| 1 - 8 | Pads del sampler (al soltar, se suelta el pad) |
| Ctrl+1 - 9 (o F1 - F9) | Pasar a la escena 1 - 9 (en portátiles HP las F necesitan la tecla Fn) |
| V | Pestaña AUDIO / PROGRAMA del centro |
| Ctrl+F | Buscar (Esc o Intro: salir del buscador) |
| Supr | Quitar la fuente elegida (vista previa o panel Fuentes) |

Mientras se escribe en el buscador o en un texto, las teclas escriben (no son atajos). Los botones no se
quedan con el foco al hacer clic, así que el espacio nunca vuelve a pulsar el último botón; los atajos llegan
aunque ningún control tenga el foco.

## Audio, audífonos y limitador

**Ajustes > Audio y audífonos...**: la tarjeta de sonido (tipo, salida, frecuencia y tamaño del bloque) y por
dónde van los **audífonos**:
- **Salidas 3 y 4** de una tarjeta de 4 canales (las controladoras con tarjeta propia);
- **partir la salida estéreo con un cable en Y**: izquierda = master en mono (a los parlantes), derecha =
  audífonos en mono; sirve con la salida normal de un portátil;
- **otra tarjeta de sonido** (p. ej. un adaptador USB de audífonos): se le pasa el audio por una cola de 40 ms
  que corrige la diferencia de reloj entre las dos tarjetas.

Con audífonos aparece un botón de **pre-escucha** bajo el volumen de cada deck (se oye tras el EQ y el filtro
y antes del volumen: el deck se puede escuchar con el fader abajo) y, en la página AUDIO, **Cue / Mst** (de solo
la pre-escucha a solo el master) y **Auricular** (volumen). Sin ningún deck en pre-escucha los audífonos oyen el
master. La pre-escucha también se asigna por MIDI.

El **limitador** del master (activado por defecto, se quita en el mismo diálogo) no deja que la salida pase de
-0,3 dBFS: baja la ganancia al momento y la recupera en 150 ms. Mientras actúa, el valor bajo los volúmenes
dice **LIM** en rojo. Todo se guarda en `%APPDATA%\MiDJ\audio.xml`.

## Biblioteca

Árbol de la izquierda:
- **Música local** (Música = solo audio, Videos = solo video).
- **Favoritos**: las canciones con la **estrella** encendida (clic en la estrella de la lista o clic derecho >
  Agregar a favoritos).
- **Más reproducidos**: las 50 que más han sonado (cuenta una vez por carga, a los 30 s sonando en un deck).
- **Listas**: listas de reproducción propias. Clic derecho en "Listas" > Nueva lista; en una canción >
  **Agregar a lista** (o a una nueva); arrastrar canciones encima de una lista. Dentro de una lista, clic
  derecho > Quitar de esta lista / Mover arriba / Mover abajo; en la lista del árbol: renombrar, borrar (las
  canciones no se borran) o reproducirla en el automix.
- **YouTube** (ver abajo).
- **Mis carpetas** (las añadidas) y **Discos** (cada disco y las carpetas donde hay canciones). La **x** de una
  carpeta o un disco lo quita de la biblioteca sin borrar archivos (las canciones quedan ocultas con su
  análisis y no vuelven al re-escanear) y sale un aviso con **Deshacer**. **Sincronizar** (flechas en círculo)
  busca música y/o video en los discos elegidos (sin Windows, Archivos de programa ni AppData) y añade las
  carpetas nuevas, que se analizan en segundo plano. Mientras se analiza el BPM, el árbol y la lista no se
  rehacen: solo cambian el BPM de las filas que se ven y el avance de abajo (así va fluida con cientos de
  miles de canciones; el orden por BPM se pone al día al buscar o elegir otra carpeta). El análisis va en
  un hilo de prioridad baja (el disco se lee a ritmo normal: el modo segundo plano de Windows lo frenaba
  demasiado), sigue desde la última canción sin
  recorrer la tabla y lee el audio a mono por trozos (menos memoria). Una reproducción contada solo cambia
  "Más reproducidos", y mientras busca canciones la lista se rehace como mucho cada 5 s. Los MP3 de 22 050,
  24 000 o 16 000 Hz (MPEG-2) salían con el doble de duración (el MP3 de JUCE cuenta mal sus tramas); ahora se
  leen bien y, la primera vez que se abre esta versión, se corrige la duración guardada de los que ya estaban.

Cada canción tiene **A** / **B** (cargar en ese deck) y **+** (al automix); doble clic la carga en el deck libre.
Favoritos, reproducciones y listas se guardan en `library.db`.

## YouTube

**Biblioteca > YouTube**: se escribe en el buscador y se pulsa **Intro** (o se pega un enlace de YouTube).
Salen unos 20 resultados; al bajar hasta el final de la lista se cargan 20 más, como en YouTube (sin Shorts
ni directos). Cada video tiene **Ver** (suena ya) y **+** (a la lista del automix de YouTube); clic derecho, también "como el
siguiente". Lo que suena se ve en la pestaña **YOUTUBE** del panel del automix: título, estado (Cargando,
Anuncio, Sonando...), avance (clic en la barra: saltar), Play/Pausa, Detener (cierra el reproductor) y volumen.

- El video se reproduce en una ventana con **WebView2** (el motor de Edge) que está fuera de las pantallas y
  solo existe mientras hay un video (unos 400 MB de RAM mientras está abierta). Abre la página normal de
  YouTube, así que se ven también los videos que no se dejan incrustar; los anuncios se ven igual que en el
  navegador. Pide H.264 a 720p, que decodifica la tarjeta gráfica. Los subtítulos de YouTube (también los
  automáticos) se apagan: no salen en la salida de video.
- Al **programa** llega con la fuente de escena **YouTube** (Añadir fuente > YouTube), que captura esa ventana.
  Con una escena con "Pasar a esta escena automáticamente > Cuando suena YouTube", el programa pasa a ella
  sola mientras suena YouTube (manda sobre los decks).
- El **sonido** sale por la tarjeta predeterminada de Windows: no pasa por el mezclador de MiDJ (sin EQ ni
  crossfader); su volumen es el de la pestaña YOUTUBE.
- **Cuenta de YouTube** (por ejemplo **Premium**, sin anuncios): el reproductor tiene su propio perfil (en
  `%APPDATA%\MiDJ\WebView2`) y no ve la sesión de Chrome ni de Edge. En la pestaña YOUTUBE, el botón de la
  calidad (*720p*) > **Iniciar sesión...** abre una ventana con el inicio de sesión de Google; al terminar
  vuelve a YouTube y se cierra sola. La sesión queda guardada y vale para los videos que se abran después. En
  el mismo menú: ver la cuenta o **Cerrar sesión** (borra las cookies del reproductor).
- El **automix de YouTube** es el mismo automix con su propia lista (pestaña YOUTUBE, Encendido/Apagado,
  siguiente y vaciar): al terminar un video suena el siguiente; un video que no se puede ver se salta. Sin
  automix, al terminar se cierra el reproductor. La lista se guarda en `youtube.xml`.
- La ventana del reproductor aparece agrupada bajo el icono de MiDJ en la barra de tareas: si se quita de
  ahí, Windows no deja capturarla.
- Si YouTube cambia su página y algo deja de ocultarse o de funcionar, el script del reproductor se puede
  cambiar sin recompilar dejando un `youtube.js` en `%APPDATA%\MiDJ`.

## Automix

Panel **AUTOMIX** abajo, entre las canciones y las escenas: se le arrastran canciones (o clic derecho en una
canción > **Agregar a automix**, o tecla A). Encendido/Apagado, Mezclar ya, Saltar, Quitar, Vaciar y la
duración del fundido. **Listas**: reproducir (la lista pasa a ser esa y se enciende) o añadir al final los
Favoritos, los Más reproducidos o una lista propia. La pestaña **YOUTUBE** es el automix de YouTube, con su
propia lista (ver YouTube). En las dos listas cada fila tiene **▲ ▼** (subir / bajar) y **x** (quitar), y se
puede **arrastrar** a otro sitio de la lista (una raya marca dónde cae); en la de los decks, clic derecho también
para subirla al principio o bajarla al final.
Cada transición se planifica como el modo Smart de VirtualDJ: **a tempo** (16 pulsos, compases alineados y el
tempo deslizándose de una canción a la otra) si la actual tiene fin de parte rítmica, la siguiente tiene inicio
y sus BPM difieren menos de un 6 %; si no, **fundido**. La lista se guarda al cerrar.
Con el automix encendido se puede **pausar** la canción que suena: se queda parada (no pasa a la siguiente)
y al darle Play sigue. Si se da Play a mano al otro deck, el crossfader pasa a ese, el anterior se pausa y el
automix sigue desde ahí, con la siguiente de la lista en el deck que quedó libre.

## Stems (voz / instrumental)

Botón **STEMS** de cada deck: separa la voz con IA en segundo plano (en este equipo tarda algo más que la
canción) y después aparecen **VOZ** e **INSTR** para apagar cada parte. También desde el navegador: clic derecho >
**Preparar stems**. La voz se guarda en `%APPDATA%\MiDJ\Stems` (FLAC) y la próxima vez se lee de ahí en segundos.
Por defecto se usa la CPU; la tarjeta gráfica (DirectML) se activa escribiendo `<STEMS device="gpu"/>` en
`%APPDATA%\MiDJ\stems.xml` (en la Radeon R5 de este portátil DirectML se cuelga, por eso no se prueba sola).

## Video

Los MP4, MOV, M4V, WMV y 3GP se cargan en un deck como cualquier canción (también M4A, AAC y WMA), con los
codecs de Windows (Media Foundation, sin FFmpeg). Pestaña **PROGRAMA** del centro (tecla V): la vista previa
del programa, que por defecto mezcla los dos decks según el crossfader (una canción sin video se ve como un
rótulo con el título y el artista). El video sigue la posición que se oye del deck: pitch, saltos, CUE y bucles.

Como en OBS, un compositor en la tarjeta gráfica (Direct3D 11) arma la imagen una vez por fotograma a ritmo
fijo (1280x720 a 30 fps) y la dibuja en cada salida; el fundido entre decks se hace en luz lineal (a la mitad
no oscurece). Salidas:
- **Vista previa** (pestaña PROGRAMA), para el DJ. Clic derecho > **Vista previa grande**: la misma en su propia
  ventana, para colocar las fuentes con comodidad.
- **Proyectores a pantalla completa**, uno por monitor, sin bordes ni cursor: doble clic en la vista previa
  abre o cierra el del segundo monitor; clic derecho, el menú con todas las pantallas y el estado del
  compositor (tarjeta, ms por fotograma, fotogramas perdidos). En el proyector: Esc o doble clic lo cierra.

Solo se compone y se decodifica mientras se ve la pestaña PROGRAMA, la vista previa grande o un proyector (el
sonido de las fuentes multimedia sigue aunque no se vea). El video se decodifica en la
tarjeta gráfica (DXVA) y, si falla, en la CPU. Ajustes en `%APPDATA%\MiDJ\video.xml`:
`<VIDEO device="cpu" width="1280" height="720" fps="30"/>` (`device="cpu"` fuerza la CPU; el resto, el
tamaño y el ritmo del programa). En este portátil (A8 + Radeon R5, Release) un video en un deck cuesta ~34 %
de un núcleo con la vista previa y ~5 % más con un proyector; componer, ~1,2 ms por fotograma.

### Escenas

Abajo a la derecha están los paneles **Escenas** y **Fuentes**, como los de OBS (se guarda en
`%APPDATA%\MiDJ\scenes.xml`; por defecto, una escena "Mezcla" con la mezcla de los decks):
- **Escenas**: clic para ponerla al aire con la transición elegida bajo el crossfader (Corte, Fundido,
  A negro, Deslizar o Barrido, de 0,1 a 3 s, o una propia); Ctrl+1-9 (o F1-F9) pasan a las nueve primeras.
  Botones para crear (la escena nueva empieza vacía, sin fuentes), borrar, renombrar y ordenar; clic derecho
  para duplicar y para **Pasar a esta escena
  automáticamente**: cuando lo que más se oye de los decks es una canción de audio, un video, cuando no suena
  nada (tras 1,5 s) o cuando suena YouTube (un icono en la lista lo indica). Solo actúa al cambiar: una escena
  elegida a mano se respeta hasta el próximo cambio. Con una **capturadora** visible en la escena al aire no
  se cambia sola: se puede poner música y se sigue viendo la capturadora (a mano sí se cambia).
  **Altavoz** (en las escenas con multimedia o capturadora): clic para silenciar la escena o devolverle el
  sonido (también en el clic derecho); sin sonido, sus fuentes no se oyen por el master. Una fuente que
  está en varias escenas suena como la de la escena al aire.
- **Fuentes** de la escena (la de arriba se ve encima), con su ojo (mostrar u ocultar) y su candado (no se
  mueve en la vista previa). Botones: **+** abre **Añadir fuente** (a la izquierda los tipos: Mezcla de decks,
  Deck A, Deck B, YouTube, Captura de pantalla, Captura de ventana, Dispositivo de captura de video, Imagen,
  Multimedia, Texto, Color y Visualizador; a la derecha "Crear nueva fuente" o "Añadir existente", la misma
  fuente de otra escena, que comparte la captura o el video), quitar, propiedades y subir/bajar. Doble clic
  o el engranaje: **Propiedades para 'fuente'**, con su imagen arriba, sus ajustes (se ven al momento), la
  colocación (opacidad, giro, recorte, Ajustar a la pantalla, Restablecer) y Por defecto / Aceptar /
  Cancelar (Cancelar deja todo como estaba). En el texto, `{titulo}`, `{artista}` y `{bpm}` se cambian por la
  canción del deck que más se ve (por ejemplo "Ahora suena: {titulo}"); si suena YouTube y ningún deck, por el
  video de YouTube (artista = el canal, sin BPM). `{yt_titulo}` y `{yt_canal}` son siempre los del video de
  YouTube abierto (vacíos si no hay ninguno).
- En la vista previa: clic para elegir una fuente (recuadro rojo), arrastrar para moverla (se pega a los
  bordes y al centro; Mayús para no pegarse), arrastrar una esquina para escalarla sin deformar (Mayús para
  deformar), flechas para mover de 1 en 1 (Mayús: de 10 en 10) y Supr para quitarla.
- **Transiciones propias** (**+ Personalizada**, bajo el crossfader): nombre y tipo, con **Probar en el
  programa** (se hace sin cambiar de escena; un barrido va a negro y vuelve, que de la escena a ella misma no
  se vería) y **Añadir transición** (queda elegida). La ventana se abre abajo a la izquierda para que se vea
  la salida de video al probar. Su **lápiz**: **Modificar...** (la misma ventana con sus datos y **Guardar
  cambios**) o **Quitar**:
  - **Stinger**: un video tapa el cambio; la escena cambia debajo en el **punto de cambio** y su sonido va al
    master. Al elegir el video se toma su duración y el punto de cambio a la mitad. Media Foundation no da
    transparencia: el video se ve entero, salvo un **video con mate** (ver abajo; la casilla se marca sola al
    elegir uno de doble ancho), que deja ver la escena por donde es transparente.
  - **Barrido**: la escena nueva entra por donde una imagen en grises es más oscura, con el borde suavizado
    (en la GPU); sin imagen, un barrido en diagonal.
  - **A color**: funde al color elegido y del color a la escena nueva.
  Se guardan en `scenes.xml` con las escenas.

### Videos con mate (transparencia)

Media Foundation no decodifica la transparencia (ni WebM VP9 con alfa ni ProRes 4444), así que MiDJ usa
videos **con mate**, como el "Track Matte" de OBS: un MP4 H.264 normal del doble de ancho con el color en
la mitad izquierda y la transparencia en grises en la derecha (blanco = se ve). Se decodifica en la tarjeta
gráfica y el compositor recorta el color con el mate. Sirve para **stingers** y para fuentes **Multimedia**
encima de otras (logos animados, tercios inferiores...): casilla **Video con mate** en sus propiedades (la
fuente mide la mitad de ancho y se vuelve a encajar).

**Automático:** al elegir un `.webm`, `.mov` o `.mkv` para una fuente Multimedia o un stinger, MiDJ mira con
[FFmpeg](https://ffmpeg.org) si tiene transparencia y, si la tiene, lo convierte (ventana con el avance y
Cancelar), usa el convertido y marca **Video con mate**. El convertido queda junto al original como
`<nombre>_mate.mp4` (si esa carpeta no deja escribir, en `%APPDATA%\MiDJ\Mate`); la próxima vez se usa sin
convertir. Cada mitad cabe en el tamaño del programa sin deformar. FFmpeg se busca en `%APPDATA%\MiDJ\ffmpeg`,
junto a `MiDJ.exe`, en el PATH y en `D:\Tools\ffmpeg\bin`; si no está, MiDJ ofrece descargarlo (la
compilación GPL de BtbN en GitHub, unos 200 MB, una sola vez: solo guarda `ffmpeg.exe` en `%APPDATA%\MiDJ\ffmpeg`).

**A mano**, con el script (por ejemplo para elegir otro tamaño):

```bat
powershell -ExecutionPolicy Bypass -File Resources\Tools\alpha-a-mate.ps1 D:\Videos\intro.mov
```

Deja `intro_mate.mp4` junto al original, de 2560x720 (cada mitad, del tamaño del programa; `-Width` y
`-Height` lo cambian). Con MiDJ instalado, el script está en la carpeta `Herramientas` del programa.

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

### Multimedia

Como la fuente Multimedia de OBS: un archivo de video o de audio (MP4, MOV, M4V, WMV, AVI, MKV, WebM, MP3,
M4A, WAV, FLAC...) con sus propiedades: **Archivo local** (Examinar), **Bucle**, **Reiniciar la reproducción
cuando la fuente esté activa** (al salir al aire empieza de cero y al dejar de estarlo se para), **Utilizar la
decodificación por hardware**, **No mostrar nada al terminar la reproducción**, **Cerrar archivo cuando esté
inactivo**, **Velocidad** (1-200 %), **Volumen** y **Silenciar**, y los controles Reproducir/Pausa, Detener y
Reiniciar con el tiempo. Su sonido va al master (se oye y lo ve el visualizador) y la imagen sigue al sonido;
sin pista de audio, sigue a su propio reloj. "Activa" es lo mismo que en OBS: visible en la escena al aire.

### Dispositivo de captura de video

Como el de OBS, para **capturadoras de video** (USB/HDMI) y webcams: las que Windows ve con su controlador UVC
o de Media Foundation (OBS usa DirectShow, que además ve algunas capturadoras antiguas). Propiedades:
**Dispositivo**, **Activar/Desactivar**, **Desactivar cuando no se muestre**, **Tipo de resolución/FPS**
(Dispositivo predeterminado o Personalizado, con **Resolución**, **FPS** y **Formato de video**: NV12, YUY2,
MJPG...), **Voltear verticalmente** y **decodificación por hardware** (el MJPG y la conversión a RGB en la
tarjeta gráfica). En las capturadoras USB/HDMI el sonido HDMI aparece en Windows como una entrada de audio
aparte ("USB3. 0 capture", "Digital Audio Interface"...): en **Dispositivo de audio** se elige (se propone
sola la que parece de la capturadora) y va al master con su **Volumen**, con una cola de 40 ms que corrige la
diferencia de reloj entre la capturadora y la tarjeta de sonido. El dispositivo solo se abre mientras se ve el
video (el audio sigue) y, si se desconecta, se vuelve a probar cada 2 s.

### Visualizador

Fuente que dibuja lo que suena por el master (lo que se oye, descontando la latencia de la tarjeta):
- **Espectro**: 48 barras de 40 Hz a 16 kHz, de -60 a 0 dB (con +3 dB por octava para que la música, que
  tiene más graves, se vea pareja). Suben rápido, bajan despacio y dejan una marca de pico.
- **Onda**: la forma de onda de los últimos 23 ms, empezando en un cruce por cero para que no tiemble.
- **Pulso**: un círculo que crece con el volumen y late con el pulso del deck que manda (el que suena; si
  suenan los dos, el que el crossfader deja más alto), más fuerte en el primero de cada compás, con un aro que
  sale en cada pulso. Sin ningún deck con BPM, late con los golpes de volumen.
- Propiedades: estilo y color (el resto, como cualquier fuente). Al cambiar de estilo se coloca en su sitio:
  espectro y onda abajo a lo ancho; pulso, un cuadrado en el centro.
- Se dibuja en la CPU escribiendo los píxeles directamente, en una imagen de como mucho 640 px de lado (el
  pulso, 320), y la tarjeta gráfica la escala. Coste medido en Release en este equipo (A8-7410), sobre la
  ventana de video con una canción: espectro +8 % de un núcleo, onda +3 %, pulso +10 %.

## Sampler

8 pads en la barra de arriba (teclas 1-8; el nombre del sonido sale al pasar el ratón). Clic en un pad vacío o arrastrar encima un archivo (o una canción del
navegador) para cargarlo; acepta audio y los `.vdjsample` de VirtualDJ con su tempo y bucle. Clic: disparar.
Clic derecho: grabar 4, 8 o 16 pulsos de un deck (empieza en el siguiente pulso y queda como bucle bloqueado),
modo de disparo (on/off, mantener, repetir), bucle, tempo (libre, sincronizado, bloqueado) y quitar.
El banco se guarda en `%APPDATA%\MiDJ\sampler.xml` y las grabaciones en `%APPDATA%\MiDJ\Grabaciones`.

## Controladoras MIDI

Botón **MIDI** (barra de arriba): clic en un control de la pantalla y después moverlo en la
controladora; queda asignado (verde). Clic derecho quita la asignación. Se guarda en
`%APPDATA%\MiDJ\midi.xml` y las controladoras que se conecten con la app abierta se detectan solas.

Sin controladora se puede probar con `MiDJ.exe --simular-midi archivo.txt`. Cada línea del archivo:

```text
# milisegundos  tipo(note/cc/pb)  canal  número  valor
20000 note 1 36 127
30000 cc 1 7 100
40000 pb 1 0 8192
```

## Actualizaciones

Al abrir, MiDJ mira en GitHub (una vez al día como mucho y en segundo plano) si hay una versión nueva. Si la
hay, sale un aviso con sus novedades:
- **Descargar e instalar**: baja el instalador (con su avance y Cancelar), lo abre y cierra MiDJ; el instalador
  solo pide **Actualizar**. Canciones, escenas y ajustes se conservan.
- **Omitir esta versión**: no vuelve a avisar de esa (sí de la siguiente).
- **Más tarde**: avisa otra vez mañana.

En **Ajustes** se ve la versión instalada, la casilla **Buscar actualizaciones al abrir** (para no mirar nunca
solo) y **Buscar ahora**, que avisa siempre: versión nueva, "tienes la última" o por qué no se pudo mirar.
Sin internet no pasa nada. Se guarda en `%APPDATA%\MiDJ\updates.xml`.

## Control remoto desde el celular

Botón del **celular** en la barra de arriba (junto a Atajos y Ajustes): **Control remoto desde el celular**
lo enciende (queda encendido para la próxima vez) y aparece un **código QR**. Con el celular conectado al
**mismo Wi-Fi** que el PC, se escanea con la cámara y se abre el control en el navegador (Chrome, Safari...),
sin instalar nada ni usar internet: MiDJ sirve la página y le habla directamente.

Se ve como una consola (oscura, con letra de máquina) con el logo de MiDJ arriba y el menú abajo:

- **Mezcla**: los dos decks uno encima del otro, cada uno con su título, artista, BPM, avance y tiempo que
  falta, el **Play** redondo grande (lleno del color del deck mientras suena), **CUE**, **SYNC** y a la derecha
  su **fader de volumen** vertical (se arrastra con el dedo). Debajo, el crossfader (CENTRO o "40% B") y el
  volumen del master.
- **Automix**: encender o apagar, Mezclar ya, Saltar y la lista.
- **Buscar**: el botón de la derecha cambia entre la biblioteca (**LIB>**) y YouTube (**YT>**):
  - en la biblioteca busca mientras se escribe; cada canción dice si es **AUDIO** o **VIDEO** y tiene **A** /
    **B** (cargar en ese deck) y **+** (al automix);
  - en YouTube busca al dejar de escribir (o con Intro) y muestra los videos de dos en dos con su
    **miniatura** y su duración; cada uno con **VER** (suena ya en MiDJ) y **+ LISTA** (a la lista del automix
    de YouTube). Las miniaturas las baja el celular de YouTube: sin internet en el celular se ve un recuadro
    gris. Al bajar hasta el final se cargan 20 videos más, como en YouTube.
  - El buscador empieza vacío y sin el teclado abierto, y se vacía al cambiar entre biblioteca y YouTube o
    de pestaña.
- **Pads y escenas**: los 8 pads del sampler (con el nombre de su sonido; en modo *mantener* suenan mientras
  se toca), Parar todo y las escenas (la que está al aire en verde).

Lo que se hace desde el celular se ve al momento en MiDJ y al revés. Pueden conectarse varios celulares a la
vez; el botón se pone **verde** mientras hay alguno y el panel dice cuántos.

- El código lleva una **clave**: sin ella la página abre pero no controla nada. El celular la recuerda, así
  que después basta con abrir la misma dirección (se puede guardar en la pantalla de inicio).
  **Cambiar clave** desconecta a todos (hay que volver a escanear).
- La primera vez Windows puede preguntar por el **firewall**: hay que permitir MiDJ en las **redes
  privadas**. Si la red del PC está marcada como *pública* en Windows, el celular no llega: cámbiala a
  privada en *Configuración > Red e Internet*.
- Si el PC tiene varias redes (Wi-Fi y cable, una VPN...), el panel deja elegir por cuál va la dirección.
- Usa el puerto 8765 (o el siguiente libre). Se guarda en `%APPDATA%\MiDJ\remote.xml`.
