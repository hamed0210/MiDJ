# MiDJ — contexto del proyecto para Claude

Este archivo lo mantiene Claude: al terminar cada versión o un cambio importante, actualizar
"Estado actual", "Historial" y "Próximo trabajo". Documentación (mantenerla al día): `README.md` corto y para usuarios (capturas en `docs/capturas`, enlace
de descarga a GitHub Releases), `docs/MANUAL.md` (cada función en detalle; el instalador lo copia como
Manual.md) y `docs/DESARROLLO.md` (compilar, estructura de archivos, reglas, licencias, registro de cambios).
El repositorio `github.com/hamed0210/MiDJ` es **público**: no subir datos personales (rutas, bibliotecas, sesiones).

## Qué es

Aplicación de **DJ + video para Windows** (C++20, JUCE 8.0.4), mezcla de **VirtualDJ** y **OBS** en un
solo programa, pensada para un portátil modesto (AMD A8-7410 + Radeon R5, pantalla 1366x768):
- Dos decks con keylock (Rubber Band), SYNC, CUE, bucles, pitch ±16 %, 3 efectos a tempo y stems con IA
  (Spleeter en ONNX Runtime); mezclador con EQ de 3 bandas, filtro, crossfader, limitador del master y
  audífonos con pre-escucha.
- Sampler de 8 pads (lee `.vdjsample`), automix "Smart" como VirtualDJ, controladoras MIDI con modo aprender.
- Biblioteca SQLite con análisis de BPM en segundo plano, favoritos, más reproducidos y listas.
- Video: MP4/MOV/WMV en los decks (Media Foundation, DXVA), compositor Direct3D 11 a 1280x720/30 fps,
  escenas y fuentes como OBS (capturas DXGI/WGC, capturadoras, multimedia, texto, visualizador...),
  transiciones (incluidas stinger y videos con mate), proyectores a pantalla completa.
- YouTube: búsqueda sin clave, reproductor WebView2 fuera de pantalla, fuente de escena y automix propio.
- Instalador Inno Setup que actualiza en su sitio, y aviso de versiones nuevas desde GitHub Releases.

Toda la interfaz, el código, los comentarios, los commits y el README están **en español** (comentarios
sin tildes en el código, en general). El usuario (Hamed) habla español: responderle en español.

## Compilar, probar, ejecutar

```bat
D:\Tools\cmake\bin\cmake --preset vs2022
D:\Tools\cmake\bin\cmake --build --preset debug      (o --preset release)
D:\Tools\cmake\bin\ctest --preset debug
build\MiDJ_artefacts\Debug\MiDJ.exe
"%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" Installer\MiDJ.iss   -> build\installer\MiDJ-Setup-<ver>.exe
```

Dependencias en `D:\Tools` (JUCE, rubberband, sqlite, onnxruntime, models\spleeter-2stems-int8, WebView2,
ffmpeg, cmake). `MiDJTests` además de las pruebas tiene herramientas (`--analyse`, `--stretch`, `--stems`,
`--video`, `--media-idle`, `--load-cost`, `--benchmark`...; ver docs/DESARROLLO.md). `MiDJ.exe --simular-midi archivo.txt`
simula una controladora.

## Arquitectura

- `MiDJEngine` (librería estática): `Source/Engine`, `Video`, `Control`, `Library`, `Brand`, `App`. Sin interfaz,
  la usan la app y las pruebas. Al añadir un .cpp hay que listarlo en `CMakeLists.txt`.
- `MiDJ` (app): `Source/Main.cpp`, `MainComponent.*` (une motor e interfaz) y `Source/UI/*`.
- `Tests/TestMain.cpp`: todas las pruebas en un solo archivo.
- Datos del usuario en `%APPDATA%\MiDJ`: `library.db`, `scenes.xml`, `layout.xml`, `window.txt`,
  `audio.xml`, `video.xml`, `stems.xml`, `midi.xml`, `sampler.xml`, `youtube.xml`, `Stems\`, `WebView2\`,
  `ffmpeg\`, `Mate\`, opcional `youtube.js`.
- Mapa de archivos detallado: sección "Estructura" de `docs/DESARROLLO.md`.

## Reglas que hay que respetar

- Hilo de audio (`getNextAudioBlock`): nunca reservar memoria, bloquear, leer disco ni tocar la interfaz.
  Lo lento en hilos aparte; cambios de volumen siempre con rampa. Para lo compartido con fuentes de
  escena se usa try-lock (no cortar el master).
- Rendimiento importa mucho (equipo lento): medir CPU en Release antes/después, no redibujar ni
  decodificar lo que no se ve, mantener la GPU para video (sin copias a la CPU).
- La biblioteca debe ir fluida con cientos de miles de canciones (no rehacer listas enteras mientras analiza).
- Comportamiento y nombres "como en OBS" / "como en VirtualDJ" cuando aplique.
- Cada arreglo o función nueva lleva su prueba en `Tests/TestMain.cpp` cuando es del motor.

## Cómo se publica una versión

1. Subir la versión en `CMakeLists.txt` (`project(... VERSION x)`, el programa la toma de ahí como
   `MIDJ_VERSION`) y en `Installer/MiDJ.iss` (`#define AppVersion`); el ejemplo "Actualizar MiDJ a -> b"
   de `docs/DESARROLLO.md`.
2. Actualizar `docs/MANUAL.md`, el "Registro de cambios" de `docs/DESARROLLO.md` y, si cambia algo
   visible, el README y sus capturas.
3. Compilar Release, pasar `ctest`, armar el instalador.
4. **Publicar en GitHub Releases** (si no, nadie recibe la actualización): etiqueta `vX.Y.Z`, novedades
   empezando por `## Novedades` y el `MiDJ-Setup-X.Y.Z.exe` adjunto. `gh` instalado (winget, cuenta
   hamed0210): `gh release create vX.Y.Z build/installer/MiDJ-Setup-X.Y.Z.exe --title "MiDJ X.Y.Z" --notes-file ...`.
   Si `gh` no está en el PATH del shell: `%LOCALAPPDATA%\Microsoft\WinGet\Packages\GitHub.cli_Microsoft.Winget.Source_8wekyb3d8bbwe\bin\gh.exe`.
   Publicar es visible para todos: confirmar con el usuario antes.
5. Commit con título `MiDJ x.y.z: resumen` (o `Version x.y.z: ...`) y cuerpo con viñetas por área
   (qué fallaba, por qué, qué se hizo y cifras medidas). Rama `main`. Solo hacer commit si el usuario lo pide.

## Estado actual

Versión **0.4.0** (2-nov-2026), compilada, probada (ctest OK) y con instalador en `build/installer`, aún
sin commit ni publicar en GitHub Releases. En GitHub la última publicada es la **0.3.5** (de la 0.3.6 a la
0.3.10 no se subieron).

## Historial (resumen por versión)

- **Base (antes de 0.2.0)**: esqueleto de dos decks y BPM → SYNC/CUE/bucles → mezclador por deck →
  keylock Rubber Band y BPM fiable → biblioteca SQLite → efectos a tempo → MIDI → sampler → automix con
  puntos de mezcla → stems con IA → video en decks y en la GPU → compositor y proyectores → escenas,
  fuentes y transiciones como OBS → capturas de pantalla/ventana → visualizador → fuentes Multimedia y
  capturadora → interfaz VirtualDJ+OBS → interfaz "Escenario" (v1 y v2) → transiciones propias →
  favoritos/más reproducidos/escenas automáticas/atajos → audífonos y limitador → listas propias → YouTube.
- **0.2.0**: README al día e instalador Inno Setup.
- **0.3.0**: arreglos de escenas, fuentes, biblioteca y YouTube; captura de ventanas sin cuelgues.
  Después: logo nuevo (vinilo) e instalador que actualiza.
- **0.3.1**: Play durante la carga, aviso de tarjeta de sonido, medidor de CPU del audio.
- **0.3.2**: videos con mate, transiciones editables, crossfader automático al dar Play.
- **0.3.3**: título de YouTube en los textos (`{yt_titulo}`, `{yt_canal}`).
- **0.3.4**: iniciar sesión en YouTube (Premium) y más resultados al bajar.
- **0.3.5**: videos con transparencia (webm/mov/mkv) se convierten solos a mate con FFmpeg.
- **0.3.6**: Play sin tarjeta de sonido (reloj interno) y cambio automático de salida de Windows.
- **0.3.7**: biblioteca fluida mientras analiza BPM.
- **0.3.8**: ordenar/arrastrar en las listas del automix; logo en su sitio al abrir.
- **0.3.9**: escenas con capturadora no cambian solas; silenciar escena; fuente en reposo de ~300 % a
  15 % de CPU; análisis de biblioteca con menos memoria.
- **0.3.10**: carga fluida de canciones largas (onda en máscara); `Mp3Format` propio que corrige la
  duración doble de los MP3 MPEG-2/2.5 de JUCE y corrección única en la biblioteca.
- **0.3.11** (28-oct, hecha en otra sesión): pausar a mano con el automix ya no salta a la siguiente;
  crossfader automático también con el automix encendido (salvo mientras mezcla).
- **0.4.0**: buscar actualizaciones en GitHub Releases (`App/Updates.*`, `UI/UpdateDialog.*`; al abrir una
  vez al día y *Ajustes > Buscar ahora*; descarga e instala); versión desde CMake (`MIDJ_VERSION`); README
  para usuarios con capturas y enlace de descarga, manual en `docs/MANUAL.md` y lo técnico en
  `docs/DESARROLLO.md`; `MiDJTests --updates`.

## Problemas conocidos y limitaciones

- DirectML (stems en GPU) se cuelga en la Radeon R5 de este portátil: por defecto stems en CPU.
- Media Foundation no da alfa: por eso los videos con mate. Capturadoras solo UVC/MF (no DirectShow).
- Windows 10 pone borde amarillo en la captura de ventana (WGC).
- YouTube depende de la página de YouTube (se puede parchear con `youtube.js` sin recompilar); su
  sonido no pasa por el mezclador.
- Rubber Band es GPL: para vender MiDJ habría que licenciarla o cambiarla (SoundTouch). Solo la usa
  `TimeStretcher.*`.

## Próximo trabajo

Apuntar aquí lo que el usuario pida y tacharlo al terminarlo.

**Pedido por el usuario (2-nov-2026), investigado, pendiente de decidir:**
- **Control remoto desde el celular** sincronizado con un código QR. Opciones estudiadas:
  (A) servidor HTTP + WebSocket dentro de MiDJ en la red local; el QR abre `http://<ip-del-PC>:<puerto>/?k=<clave>`
  y MiDJ sirve la página (sin internet ni servidores; mismo Wi-Fi; aviso del firewall de Windows la 1.ª vez).
  (B) página en la web (GitHub Pages) + relé WebSocket en la nube (p. ej. Cloudflare Worker con Durable
  Objects, gratis): el QR lleva el código de sala; funciona con datos móviles, pero necesita internet y un
  servidor que mantener. Una página https no puede hablar con `ws://` de la red local (contenido mixto), por
  eso (B) necesita el relé. Recomendado: empezar por (A) y añadir (B) después con el mismo protocolo.
- **Spotify**: la API pública lo prohíbe para un DJ (Developer Policy III.7: no mezclar ni solapar
  contenido; III.6: no sincronizar con video; Web Playback SDK solo Premium y sin uso comercial; desde
  mayo-2025 el acceso para muchos usuarios es solo para empresas grandes y el modo desarrollo admite 5
  usuarios). rekordbox, Serato, djay (y según una fuente VirtualDJ desde sep-2026) lo tienen por acuerdo
  privado con Spotify, no por la API. Lo viable: importar listas de Spotify (solo títulos) para buscar
  esas canciones en la biblioteca local o en YouTube, o pedir ser socio de Spotify.

Candidatos que salen de las limitaciones de arriba (sin confirmar):
- Sonido de YouTube por el mezclador de MiDJ (EQ, crossfader).
- Capturadoras por DirectShow para las antiguas.
- Sustituto de Rubber Band si se quiere distribuir comercialmente.
