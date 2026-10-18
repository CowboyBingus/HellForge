# The launcher's icons from HellForge's icon (native/editor/assets/hellforge-icon.png, 512 x 512): a Windows .ico of
# PNG images at 16 to 256 pixels (the executable's and the window's icon) and a 128-pixel PNG for the sidebar, into
# src/HellForge.Launcher/Assets. Run it again after the icon changes.
#   pwsh scripts/Build-LauncherIcon.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$project = Split-Path -Parent $PSScriptRoot
$source = Join-Path $project 'native/editor/assets/hellforge-icon.png'
$assets = Join-Path $project 'src/HellForge.Launcher/Assets'
New-Item -ItemType Directory -Force $assets | Out-Null

$image = [System.Drawing.Image]::FromFile($source)
function Resized([int]$size) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.DrawImage($image, 0, 0, $size, $size)
    $g.Dispose()
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    return ,$stream.ToArray()
}

$sizes = 16, 24, 32, 48, 64, 128, 256
$pngs = foreach ($size in $sizes) { ,(Resized $size) }
$out = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $out
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count) # ICONDIR
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $dimension = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }
    $writer.Write([byte]$dimension); $writer.Write([byte]$dimension); $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32); $writer.Write([uint32]$pngs[$i].Length); $writer.Write([uint32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($png in $pngs) { $writer.Write($png) }
$writer.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $assets 'hellforge.ico'), $out.ToArray())
[System.IO.File]::WriteAllBytes((Join-Path $assets 'hellforge-icon.png'), (Resized 128))
$image.Dispose()
Write-Host "Launcher icons written to $assets"
