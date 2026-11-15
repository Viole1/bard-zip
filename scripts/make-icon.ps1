<#
.SYNOPSIS
    Generates resources/bardzip.ico (the application icon) with System.Drawing.

.DESCRIPTION
    The icon is one stanza of a poem: three lines on a dark violet rounded
    square - two verses and the shorter golden refrain. The result is a
    multi-size .ico (16..256 px) with PNG-compressed images. The generated
    file is committed; run this script only to change the design.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\make-icon.ps1
#>
param(
    [string]$Output
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

if (-not $Output) { $Output = Join-Path (Split-Path -Parent $PSScriptRoot) "resources\bardzip.ico" }

function New-RoundedPath([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $path.AddArc($x, $y, $d, $d, 180, 90)
    $path.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $path.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $path.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-IconBitmap([int]$size) {
    $bitmap = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    # Background: rounded square with a violet gradient.
    $margin = $size * 0.04
    $bg = New-RoundedPath $margin $margin ($size - 2 * $margin) ($size - 2 * $margin) ($size * 0.2)
    $gradient = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF(0, 0)), (New-Object System.Drawing.PointF($size, $size)),
        [System.Drawing.Color]::FromArgb(255, 88, 56, 168), [System.Drawing.Color]::FromArgb(255, 36, 24, 84))
    $g.FillPath($gradient, $bg)

    # Three lines of a stanza: two verses and the shorter golden refrain.
    $left = $size * 0.22
    $thick = [Math]::Max(1.5, $size * 0.085)
    $lines = @(
        @{ Y = 0.33; Length = 0.56; Color = [System.Drawing.Color]::FromArgb(255, 245, 240, 255) },
        @{ Y = 0.52; Length = 0.46; Color = [System.Drawing.Color]::FromArgb(255, 245, 240, 255) },
        @{ Y = 0.71; Length = 0.34; Color = [System.Drawing.Color]::FromArgb(255, 255, 196, 64) }
    )

    foreach ($line in $lines) {
        $pen = New-Object System.Drawing.Pen($line.Color, $thick)
        $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        $y = $size * $line.Y
        $g.DrawLine($pen, $left, $y, $left + $size * $line.Length, $y)
        $pen.Dispose()
    }

    $g.Dispose()
    return $bitmap
}

$sizes = 16, 24, 32, 48, 64, 128, 256
$images = foreach ($size in $sizes) {
    $bitmap = New-IconBitmap $size
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    [pscustomobject]@{ Size = $size; Data = $stream.ToArray() }
}

# ICO file: header, one directory entry per image, then the PNG data.
$file = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter($file)
$writer.Write([uint16]0)               # reserved
$writer.Write([uint16]1)               # type: icon
$writer.Write([uint16]$images.Count)

$offset = 6 + 16 * $images.Count
foreach ($image in $images) {
    $dimension = if ($image.Size -ge 256) { 0 } else { $image.Size }
    $writer.Write([byte]$dimension)    # width (0 = 256)
    $writer.Write([byte]$dimension)    # height
    $writer.Write([byte]0)             # palette
    $writer.Write([byte]0)             # reserved
    $writer.Write([uint16]1)           # colour planes
    $writer.Write([uint16]32)          # bits per pixel
    $writer.Write([uint32]$image.Data.Length)
    $writer.Write([uint32]$offset)
    $offset += $image.Data.Length
}
foreach ($image in $images) { $writer.Write($image.Data) }
$writer.Flush()

New-Item -ItemType Directory -Force (Split-Path -Parent $Output) | Out-Null
[System.IO.File]::WriteAllBytes($Output, $file.ToArray())
"Created $Output ($($file.Length) bytes, sizes: $($sizes -join ', '))"

# A PNG preview of the largest size next to the icon, for looking at the design.
$preview = [System.IO.Path]::ChangeExtension($Output, ".png")
[System.IO.File]::WriteAllBytes($preview, ($images | Where-Object { $_.Size -eq 256 }).Data)
"Created $preview"
