# Genera las imagenes del logo a partir de icon.svg e icon-small.svg (Windows PowerShell 5.1):
#   powershell -ExecutionPolicy Bypass -File Resources\Logo\render.ps1
# Edge (sin ventana) dibuja cada imagen a su tamano; System.Drawing la recorta y arma el .ico.
# Deja aqui: icon-256.png e icon-32.png (icono del programa, ver CMakeLists.txt), MiDJ.ico
# (instalador), logo.png (README) y las imagenes del asistente del instalador (wizard-*.png).

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$here = $PSScriptRoot
$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe", "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw 'No se encuentra Microsoft Edge' }
$work = Join-Path $env:TEMP ('midj-logo-' + [guid]::NewGuid())
New-Item -ItemType Directory $work | Out-Null

$font = '<link href="https://fonts.googleapis.com/css2?family=Unbounded:wght@800&display=swap" rel="stylesheet">'
$page = '<!doctype html><html><head><meta charset="utf-8">{0}<style>html,body{{margin:0;background:transparent}}</style></head><body>{1}</body></html>'

# Dibuja $body (HTML) y guarda el rectangulo de arriba a la izquierda de $w x $h.
function Render ($body, $w, $h, $out, $withFont = $false) {
    $html = Join-Path $work 'page.html'
    $shot = Join-Path $work 'shot.png'
    [IO.File]::WriteAllText($html, ($page -f ($(if ($withFont) { $font } else { '' }), $body)))
    Remove-Item $shot -ErrorAction SilentlyContinue
    # Ventana de al menos 400 px: Edge no hace capturas mas pequenas; luego se recorta.
    $size = "$([Math]::Max(400, $w)),$([Math]::Max(400, $h))"
    # Start-Process: lo que Edge escribe en stderr no cuenta como error del script.
    Start-Process $edge -Wait -WindowStyle Hidden -RedirectStandardError (Join-Path $work 'edge.log') -ArgumentList @(
        '--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run', "`"--user-data-dir=$work\profile`"",
        '--default-background-color=00000000', '--force-device-scale-factor=1', '--virtual-time-budget=8000',
        "--window-size=$size", "`"--screenshot=$shot`"", ('"file:///' + $html.Replace('\', '/') + '"'))
    for ($i = 0; $i -lt 50 -and -not (Test-Path $shot); $i++) { Start-Sleep -Milliseconds 200 }
    $src = [Drawing.Bitmap]::FromFile($shot)
    $dst = $src.Clone((New-Object Drawing.Rectangle 0, 0, $w, $h), [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $src.Dispose()
    $dst.Save($out, [Drawing.Imaging.ImageFormat]::Png)
    $dst.Dispose()
}

function SvgUrl ($name) { 'file:///' + (Join-Path $here $name).Replace('\', '/') }

# Iconos: hasta 32 px el dibujo simplificado (los surcos y la cabeza del brazo no se ven).
$sizes = 16, 20, 24, 32, 40, 48, 64, 96, 128, 256
$pngs = @{}
foreach ($s in $sizes) {
    $svg = if ($s -le 32) { 'icon-small.svg' } else { 'icon.svg' }
    $out = Join-Path $work "icon-$s.png"
    Render "<img src=`"$(SvgUrl $svg)`" style=`"display:block;width:${s}px;height:${s}px`">" $s $s $out
    $pngs[$s] = $out
}
Copy-Item $pngs[256] (Join-Path $here 'icon-256.png') -Force
Copy-Item $pngs[32] (Join-Path $here 'icon-32.png') -Force

# .ico con cada tamano guardado como PNG (Windows Vista o posterior).
$ico = New-Object IO.MemoryStream
$bw = New-Object IO.BinaryWriter $ico
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
$data = foreach ($s in $sizes) { , [IO.File]::ReadAllBytes($pngs[$s]) }
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]
    $b = if ($s -ge 256) { 0 } else { $s }
    $bw.Write([Byte]$b); $bw.Write([Byte]$b); $bw.Write([Byte]0); $bw.Write([Byte]0)
    $bw.Write([UInt16]1); $bw.Write([UInt16]32)
    $bw.Write([UInt32]$data[$i].Length); $bw.Write([UInt32]$offset)
    $offset += $data[$i].Length
}
foreach ($d in $data) { $bw.Write($d) }
[IO.File]::WriteAllBytes((Join-Path $here 'MiDJ.ico'), $ico.ToArray())

# Logo con el nombre (README), sobre su fondo para que se vea en temas claros y oscuros.
$wordmark = '<span style="color:#F2EFE8">Mi</span><span style="color:#FF5A36">DJ</span>'
Render ("<div style=`"width:640px;height:200px;box-sizing:border-box;border-radius:28px;background:#0E0F13;display:flex;align-items:center;justify-content:center;gap:28px`">" +
        "<img src=`"$(SvgUrl 'icon.svg')`" style=`"width:136px;height:136px`">" +
        "<div style=`"font-family:Unbounded;font-weight:800;font-size:84px;letter-spacing:-3px`">$wordmark</div></div>") 640 200 (Join-Path $here 'logo.png') $true

# Asistente del instalador: imagen grande (bienvenida y final) y pequena (resto de paginas),
# a 100, 150 y 200 % de escala.
foreach ($k in 1.0, 1.5, 2.0) {
    $w = [int](164 * $k); $h = [int](314 * $k); $m = [int](112 * $k); $f = [int](30 * $k)
    Render ("<div style=`"width:${w}px;height:${h}px;background:#0E0F13;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:$([int](18 * $k))px`">" +
            "<img src=`"$(SvgUrl 'icon.svg')`" style=`"width:${m}px;height:${m}px`">" +
            "<div style=`"font-family:Unbounded;font-weight:800;font-size:${f}px;letter-spacing:-1px`">$wordmark</div></div>") $w $h (Join-Path $here "wizard-$w.png") $true
    $s = [int](55 * $k)
    $svg = if ($s -le 32) { 'icon-small.svg' } else { 'icon.svg' }
    Render "<img src=`"$(SvgUrl $svg)`" style=`"display:block;width:${s}px;height:${s}px`">" $s $s (Join-Path $here "wizard-small-$s.png")
}

Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
Get-ChildItem $here -Include *.png, *.ico -Recurse | ForEach-Object { '{0}  {1} bytes' -f $_.Name, $_.Length }
