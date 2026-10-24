# Convierte un video con transparencia (ProRes 4444, WebM VP9 con alfa, PNG con alfa...) en un
# MP4 H.264 "con mate" para MiDJ: el color en la mitad izquierda y la transparencia en grises
# en la derecha (blanco = se ve). MiDJ lo decodifica en la tarjeta grafica y lo recorta con el mate.
#
#   powershell -ExecutionPolicy Bypass -File Resources\Tools\alpha-a-mate.ps1 video.mov [-Width 1280] [-Height 720]
#
# Deja <nombre>_mate.mp4 junto al original (no lo toca). El tamano es el de cada mitad (por
# defecto el del programa, 1280x720): el archivo queda del doble de ancho. Necesita FFmpeg en
# D:\Tools\ffmpeg\bin o en el PATH.
param(
    [Parameter(Mandatory = $true)][string]$InputFile,
    [int]$Width = 1280,
    [int]$Height = 720,
    [string]$OutputFile = ''
)
$ErrorActionPreference = 'Stop'

$ffmpeg = @('D:\Tools\ffmpeg\bin\ffmpeg.exe', (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source) |
    Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $ffmpeg) { throw 'No se encuentra FFmpeg (D:\Tools\ffmpeg\bin\ffmpeg.exe o en el PATH)' }
$source = (Resolve-Path $InputFile).Path
if (-not $OutputFile) {
    $OutputFile = Join-Path (Split-Path $source) ([IO.Path]::GetFileNameWithoutExtension($source) + '_mate.mp4')
}
if (Test-Path $OutputFile) { throw "Ya existe $OutputFile" }

# WebM VP9: el decodificador propio de FFmpeg no lee la transparencia; libvpx si.
$decoder = @()
if ([IO.Path]::GetExtension($source) -ieq '.webm') { $decoder = @('-c:v', 'libvpx-vp9') }

$filter = "[0:v]scale=${Width}:${Height}:flags=lanczos,format=yuva444p,split[c][a];" +
          "[a]alphaextract,format=yuv444p[m];[c]format=yuv444p[cc];" +
          "[cc][m]hstack=inputs=2,format=yuv420p[v]"
& $ffmpeg -hide_banner -loglevel error @decoder -i $source -filter_complex $filter -map '[v]' -map '0:a?' `
    -c:v libx264 -preset medium -crf 18 -c:a aac -b:a 192k -movflags +faststart $OutputFile
if ($LASTEXITCODE -ne 0) { throw "FFmpeg fallo ($LASTEXITCODE)" }
"Listo: $OutputFile ($(2 * $Width)x$Height). En MiDJ: marca 'Video con mate' en el stinger o en la fuente Multimedia."
