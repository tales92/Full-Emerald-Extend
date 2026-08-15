param(
    [string]$Source = "",
    [string]$Destination = "",
    [string]$Preview = "",
    [string]$BrandDestination = "",
    [switch]$TransparentBlack,
    [switch]$PixelArt
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Source)) {
    $Source = Join-Path $repositoryRoot 'recomp-ui\assets\common\img\brand_mark.tga'
}
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $repositoryRoot 'assets\windows\gen3recomp.ico'
}

Add-Type -AssemblyName System.Drawing

function Read-UncompressedTga32([string]$Path) {
    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 18 -or $bytes[1] -ne 0 -or $bytes[2] -ne 2 -or
        $bytes[16] -ne 32) {
        throw "Expected an uncompressed, true-color 32-bit TGA: $Path"
    }

    $width = [int]$bytes[12] + ([int]$bytes[13] -shl 8)
    $height = [int]$bytes[14] + ([int]$bytes[15] -shl 8)
    $pixelOffset = 18 + [int]$bytes[0]
    $rowBytes = $width * 4
    if ($width -le 0 -or $height -le 0 -or
        $pixelOffset + $rowBytes * $height -gt $bytes.Length) {
        throw "Invalid TGA dimensions or pixel payload: $Path"
    }
    if (($bytes[17] -band 0x10) -ne 0) {
        throw "Right-origin TGA images are not supported: $Path"
    }

    $bitmap = [System.Drawing.Bitmap]::new(
        $width, $height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $rect = [System.Drawing.Rectangle]::new(0, 0, $width, $height)
    $data = $bitmap.LockBits(
        $rect,
        [System.Drawing.Imaging.ImageLockMode]::WriteOnly,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        $topOrigin = ($bytes[17] -band 0x20) -ne 0
        for ($y = 0; $y -lt $height; ++$y) {
            $sourceY = if ($topOrigin) { $y } else { $height - 1 - $y }
            $sourceOffset = $pixelOffset + $sourceY * $rowBytes
            $destination = [System.IntPtr]::Add($data.Scan0, $y * $data.Stride)
            [System.Runtime.InteropServices.Marshal]::Copy(
                $bytes, $sourceOffset, $destination, $rowBytes)
        }
    }
    finally {
        $bitmap.UnlockBits($data)
    }
    return $bitmap
}

function Read-PngIconBitmap([string]$Path) {
    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 22 -or $bytes[2] -ne 1) {
        throw "Expected a Windows icon: $Path"
    }
    $count = [BitConverter]::ToUInt16($bytes, 4)
    $bestOffset = 0
    $bestLength = 0
    $bestExtent = -1
    for ($i = 0; $i -lt $count; ++$i) {
        $entry = 6 + 16 * $i
        if ($entry + 16 -gt $bytes.Length) { break }
        $width = if ($bytes[$entry] -eq 0) { 256 } else { [int]$bytes[$entry] }
        $height = if ($bytes[$entry + 1] -eq 0) { 256 } else { [int]$bytes[$entry + 1] }
        $length = [BitConverter]::ToUInt32($bytes, $entry + 8)
        $offset = [BitConverter]::ToUInt32($bytes, $entry + 12)
        if ($offset + $length -gt $bytes.Length) { continue }
        if ($width * $height -gt $bestExtent) {
            $bestExtent = $width * $height
            $bestOffset = [int]$offset
            $bestLength = [int]$length
        }
    }
    if ($bestLength -lt 8 -or
        $bytes[$bestOffset] -ne 0x89 -or
        $bytes[$bestOffset + 1] -ne 0x50 -or
        $bytes[$bestOffset + 2] -ne 0x4E -or
        $bytes[$bestOffset + 3] -ne 0x47) {
        throw "The largest icon frame is not PNG encoded: $Path"
    }
    $stream = [System.IO.MemoryStream]::new(
        $bytes, $bestOffset, $bestLength, $false, $true)
    try {
        $image = [System.Drawing.Image]::FromStream($stream)
        try {
            return [System.Drawing.Bitmap]::new($image)
        }
        finally {
            $image.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Convert-BlackToTransparentAndTrim(
    [System.Drawing.Bitmap]$SourceBitmap
) {
    $transparent = [System.Drawing.Bitmap]::new(
        $SourceBitmap.Width, $SourceBitmap.Height,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $minX = $SourceBitmap.Width
    $minY = $SourceBitmap.Height
    $maxX = -1
    $maxY = -1
    for ($y = 0; $y -lt $SourceBitmap.Height; ++$y) {
        for ($x = 0; $x -lt $SourceBitmap.Width; ++$x) {
            $pixel = $SourceBitmap.GetPixel($x, $y)
            if ($pixel.R -le 4 -and $pixel.G -le 4 -and $pixel.B -le 4) {
                $transparent.SetPixel($x, $y, [System.Drawing.Color]::Transparent)
                continue
            }
            $transparent.SetPixel($x, $y, $pixel)
            if ($x -lt $minX) { $minX = $x }
            if ($x -gt $maxX) { $maxX = $x }
            if ($y -lt $minY) { $minY = $y }
            if ($y -gt $maxY) { $maxY = $y }
        }
    }
    if ($maxX -lt $minX -or $maxY -lt $minY) {
        $transparent.Dispose()
        throw "Source became empty after removing its black background"
    }
    $bounds = [System.Drawing.Rectangle]::new(
        $minX, $minY, $maxX - $minX + 1, $maxY - $minY + 1)
    $cropped = $transparent.Clone(
        $bounds, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $transparent.Dispose()
    return ,$cropped
}

function New-ScaledSquareBitmap(
    [System.Drawing.Bitmap]$SourceBitmap,
    [int]$Size,
    [double]$Fill,
    [bool]$UsePixelArt
) {
    $frame = [System.Drawing.Bitmap]::new(
        $Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($frame)
    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.CompositingMode =
            [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
        $graphics.CompositingQuality =
            [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.InterpolationMode = if ($UsePixelArt) {
            [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        } else {
            [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        }
        $graphics.PixelOffsetMode = if ($UsePixelArt) {
            [System.Drawing.Drawing2D.PixelOffsetMode]::Half
        } else {
            [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        }
        $graphics.SmoothingMode = if ($UsePixelArt) {
            [System.Drawing.Drawing2D.SmoothingMode]::None
        } else {
            [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        }

        $maxExtent = [Math]::Max(1, [int][Math]::Round($Size * $Fill))
        $scale = [Math]::Min(
            $maxExtent / [double]$SourceBitmap.Width,
            $maxExtent / [double]$SourceBitmap.Height)
        $width = [Math]::Max(1, [int][Math]::Round($SourceBitmap.Width * $scale))
        $height = [Math]::Max(1, [int][Math]::Round($SourceBitmap.Height * $scale))
        $x = [int](($Size - $width) / 2)
        $y = [int](($Size - $height) / 2)
        $graphics.DrawImage(
            $SourceBitmap,
            [System.Drawing.Rectangle]::new($x, $y, $width, $height),
            0, 0, $SourceBitmap.Width, $SourceBitmap.Height,
            [System.Drawing.GraphicsUnit]::Pixel)
    }
    finally {
        $graphics.Dispose()
    }
    return ,$frame
}

function Write-UncompressedTga32(
    [System.Drawing.Bitmap]$Bitmap,
    [string]$Path
) {
    $directory = Split-Path -Parent $Path
    if ($directory) { New-Item -ItemType Directory -Force -Path $directory | Out-Null }
    $file = [System.IO.File]::Create($Path)
    $writer = [System.IO.BinaryWriter]::new($file)
    try {
        [byte[]]$header = New-Object byte[] 18
        $header[2] = 2
        $header[12] = [byte]($Bitmap.Width -band 0xFF)
        $header[13] = [byte](($Bitmap.Width -shr 8) -band 0xFF)
        $header[14] = [byte]($Bitmap.Height -band 0xFF)
        $header[15] = [byte](($Bitmap.Height -shr 8) -band 0xFF)
        $header[16] = 32
        $header[17] = 0x28 # top-left origin, eight alpha bits
        $writer.Write($header)
        for ($y = 0; $y -lt $Bitmap.Height; ++$y) {
            for ($x = 0; $x -lt $Bitmap.Width; ++$x) {
                $pixel = $Bitmap.GetPixel($x, $y)
                $writer.Write([byte]$pixel.B)
                $writer.Write([byte]$pixel.G)
                $writer.Write([byte]$pixel.R)
                $writer.Write([byte]$pixel.A)
            }
        }
    }
    finally {
        $writer.Dispose()
    }
}

function New-IconFrame(
    [System.Drawing.Bitmap]$SourceBitmap,
    [int]$Size,
    [bool]$UsePixelArt
) {
    $fill = if ($UsePixelArt) { 0.94 } else { 0.86 }
    $frame = New-ScaledSquareBitmap $SourceBitmap $Size $fill $UsePixelArt

    $stream = [System.IO.MemoryStream]::new()
    try {
        $frame.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        return ,$stream.ToArray()
    }
    finally {
        $stream.Dispose()
        $frame.Dispose()
    }
}

$resolvedSource = (Resolve-Path -LiteralPath $Source).Path
$sourceBitmap = if ([IO.Path]::GetExtension($resolvedSource) -ieq '.ico') {
    Read-PngIconBitmap $resolvedSource
} else {
    Read-UncompressedTga32 $resolvedSource
}
try {
    if ($TransparentBlack) {
        $prepared = Convert-BlackToTransparentAndTrim $sourceBitmap
        $sourceBitmap.Dispose()
        $sourceBitmap = $prepared
    }
    [int[]]$sizes = 16, 24, 32, 48, 64, 128, 256
    $frames = foreach ($size in $sizes) {
        [pscustomobject]@{
            Size = $size
            Bytes = New-IconFrame $sourceBitmap $size ([bool]$PixelArt)
        }
    }

    $destinationDirectory = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
    $file = [System.IO.File]::Create($Destination)
    $writer = [System.IO.BinaryWriter]::new($file)
    try {
        $writer.Write([uint16]0) # reserved
        $writer.Write([uint16]1) # icon
        $writer.Write([uint16]$frames.Count)
        [uint32]$offset = 6 + 16 * $frames.Count
        foreach ($frame in $frames) {
            $encodedSize = if ($frame.Size -eq 256) { 0 } else { $frame.Size }
            $writer.Write([byte]$encodedSize)
            $writer.Write([byte]$encodedSize)
            $writer.Write([byte]0) # palette entries
            $writer.Write([byte]0) # reserved
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$frame.Bytes.Length)
            $writer.Write($offset)
            $offset += [uint32]$frame.Bytes.Length
        }
        foreach ($frame in $frames) {
            $writer.Write([byte[]]$frame.Bytes)
        }
    }
    finally {
        $writer.Dispose()
    }

    if (-not [string]::IsNullOrWhiteSpace($Preview)) {
        $previewDirectory = Split-Path -Parent $Preview
        if ($previewDirectory) {
            New-Item -ItemType Directory -Force -Path $previewDirectory | Out-Null
        }
        [System.IO.File]::WriteAllBytes(
            $Preview,
            [byte[]]($frames | Where-Object Size -eq 256).Bytes)
    }
    if (-not [string]::IsNullOrWhiteSpace($BrandDestination)) {
        $brand = New-ScaledSquareBitmap $sourceBitmap 256 0.96 ([bool]$PixelArt)
        try { Write-UncompressedTga32 $brand $BrandDestination }
        finally { $brand.Dispose() }
    }
}
finally {
    $sourceBitmap.Dispose()
}

Write-Host "Generated Windows icon: $Destination"
