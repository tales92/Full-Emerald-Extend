[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot,
    [string]$OutputPath =
        'D:\Gen3RecompLocal\assets\follower_emerald.g3fs'
)

$ErrorActionPreference = 'Stop'

function Get-FullPath([string]$Path) {
    return [IO.Path]::GetFullPath($Path)
}

$SourceRoot = Get-FullPath $SourceRoot
$OutputPath = Get-FullPath $OutputPath
if ([IO.Path]::GetPathRoot($OutputPath) -ine 'D:\') {
    throw "OutputPath must stay on D: ($OutputPath)"
}
if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) {
    throw "Sprite source directory not found: $SourceRoot"
}

Add-Type -AssemblyName System.Drawing

# Source filenames use National Dex numbers. Emerald contains National Dex
# 001..386, but its party structs use a different internal enumeration after
# Celebi because old Unown-form placeholders occupy 252..276. Generate the
# complete, contiguous National Dex set while storing audited internal ids.
$speciesEntries = @(foreach ($national in 1..386) {
    @{
        National = $national
        Internal = if ($national -le 251) { $national } else { $national + 25 }
    }
})
$directions = @('down', 'left', 'right', 'up')

function Get-FramePath(
    [int]$Species,
    [bool]$Shiny,
    [string]$Direction,
    [int]$Frame
) {
    $parts = @($SourceRoot)
    if ($Shiny) { $parts += 'shiny' }
    $parts += $Direction
    if ($Frame -eq 1) { $parts += 'frame2' }
    $directory = $parts[0]
    foreach ($part in $parts[1..($parts.Count - 1)]) {
        $directory = Join-Path -Path $directory -ChildPath $part
    }
    return Join-Path -Path $directory -ChildPath ("{0}.png" -f $Species)
}

function Read-Frames([int]$Species, [bool]$Shiny) {
    $frames = [Collections.Generic.List[Drawing.Bitmap]]::new()
    $sourceWidth = 0
    $sourceHeight = 0
    foreach ($direction in $directions) {
        foreach ($frame in 0, 1) {
            $path = Get-FramePath -Species $Species -Shiny $Shiny `
                -Direction $direction -Frame $frame
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                throw "Missing follower frame: $path"
            }
            $bitmap = [Drawing.Bitmap]::new($path)
            if (($bitmap.Width -ne 32 -or $bitmap.Height -ne 32) -and
                ($bitmap.Width -ne 64 -or $bitmap.Height -ne 64)) {
                $bitmap.Dispose()
                throw "Follower frame must be 32x32 or 64x64: $path"
            }
            if ($sourceWidth -eq 0) {
                $sourceWidth = $bitmap.Width
                $sourceHeight = $bitmap.Height
            } elseif ($bitmap.Width -ne $sourceWidth -or
                      $bitmap.Height -ne $sourceHeight) {
                $bitmap.Dispose()
                throw "Follower animation frames must share one canvas size: $path"
            }
            $frames.Add($bitmap)
        }
    }

    if ($sourceWidth -eq 64) {
        # Seven large species use a 64x64 canvas in the source pack. Find one
        # shared content rectangle across all eight frames, then apply exactly
        # one scale and one bottom-centre anchor to the entire animation. This
        # keeps direction/frame changes from shifting the follower on screen.
        $minX = $sourceWidth
        $minY = $sourceHeight
        $maxX = -1
        $maxY = -1
        foreach ($bitmap in $frames) {
            for ($y = 0; $y -lt $sourceHeight; ++$y) {
                for ($x = 0; $x -lt $sourceWidth; ++$x) {
                    if ($bitmap.GetPixel($x, $y).A -eq 0) { continue }
                    $minX = [Math]::Min($minX, $x)
                    $minY = [Math]::Min($minY, $y)
                    $maxX = [Math]::Max($maxX, $x)
                    $maxY = [Math]::Max($maxY, $y)
                }
            }
        }
        if ($maxX -lt $minX -or $maxY -lt $minY) {
            foreach ($bitmap in $frames) { $bitmap.Dispose() }
            throw "Follower animation is fully transparent: species $Species"
        }

        $contentWidth = $maxX - $minX + 1
        $contentHeight = $maxY - $minY + 1
        $scale = [Math]::Min(30.0 / $contentWidth,
                             30.0 / $contentHeight)
        $targetWidth = [Math]::Max(
            1, [int][Math]::Round($contentWidth * $scale))
        $targetHeight = [Math]::Max(
            1, [int][Math]::Round($contentHeight * $scale))
        $targetX = [int][Math]::Floor((32 - $targetWidth) / 2.0)
        $targetY = 32 - $targetHeight
        $normalized =
            [Collections.Generic.List[Drawing.Bitmap]]::new()
        try {
            foreach ($bitmap in $frames) {
                $output = [Drawing.Bitmap]::new(
                    32, 32,
                    [Drawing.Imaging.PixelFormat]::Format32bppArgb)
                $graphics = [Drawing.Graphics]::FromImage($output)
                try {
                    $graphics.CompositingMode =
                        [Drawing.Drawing2D.CompositingMode]::SourceCopy
                    $graphics.CompositingQuality =
                        [Drawing.Drawing2D.CompositingQuality]::HighSpeed
                    $graphics.InterpolationMode =
                        [Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
                    $graphics.PixelOffsetMode =
                        [Drawing.Drawing2D.PixelOffsetMode]::Half
                    $graphics.SmoothingMode =
                        [Drawing.Drawing2D.SmoothingMode]::None
                    $graphics.Clear([Drawing.Color]::Transparent)
                    $destination = [Drawing.Rectangle]::new(
                        $targetX, $targetY, $targetWidth, $targetHeight)
                    $graphics.DrawImage(
                        $bitmap, $destination,
                        $minX, $minY, $contentWidth, $contentHeight,
                        [Drawing.GraphicsUnit]::Pixel)
                } finally {
                    $graphics.Dispose()
                }
                $normalized.Add($output)
            }
        } catch {
            foreach ($bitmap in $normalized) { $bitmap.Dispose() }
            throw
        } finally {
            foreach ($bitmap in $frames) { $bitmap.Dispose() }
        }
        return $normalized
    }
    return $frames
}

function Convert-Bgr555([Drawing.Color]$Color) {
    $red = [Math]::Min(31, [int][Math]::Round($Color.R * 31.0 / 255.0))
    $green = [Math]::Min(31, [int][Math]::Round($Color.G * 31.0 / 255.0))
    $blue = [Math]::Min(31, [int][Math]::Round($Color.B * 31.0 / 255.0))
    return [uint16]($red -bor ($green -shl 5) -bor ($blue -shl 10))
}

function Write-Tiled4Bpp(
    [IO.BinaryWriter]$Writer,
    [Drawing.Bitmap]$Bitmap,
    [Collections.Generic.Dictionary[int, int]]$PaletteIndex
) {
    for ($tileY = 0; $tileY -lt 4; ++$tileY) {
        for ($tileX = 0; $tileX -lt 4; ++$tileX) {
            for ($row = 0; $row -lt 8; ++$row) {
                for ($column = 0; $column -lt 8; $column += 2) {
                    $indices = @(0, 0)
                    for ($pixel = 0; $pixel -lt 2; ++$pixel) {
                        $color = $Bitmap.GetPixel(
                            $tileX * 8 + $column + $pixel,
                            $tileY * 8 + $row)
                        if ($color.A -ne 0) {
                            if ($color.A -ne 255) {
                                throw 'Follower sprites must use binary alpha.'
                            }
                            $argb = $color.ToArgb()
                            if (-not $PaletteIndex.ContainsKey($argb)) {
                                throw 'Frame contains a color absent from its palette.'
                            }
                            $indices[$pixel] = $PaletteIndex[$argb]
                        }
                    }
                    $Writer.Write([byte]($indices[0] -bor
                                         ($indices[1] -shl 4)))
                }
            }
        }
    }
}

$outputDirectory = Split-Path -Parent $OutputPath
[IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
$temporaryPath = $OutputPath + '.tmp'
$stream = [IO.File]::Open(
    $temporaryPath, [IO.FileMode]::Create, [IO.FileAccess]::Write,
    [IO.FileShare]::None)
$writer = [IO.BinaryWriter]::new($stream, [Text.Encoding]::ASCII, $false)

try {
    $writer.Write([Text.Encoding]::ASCII.GetBytes('G3FS'))
    $writer.Write([uint16]1)
    $writer.Write([uint16]($speciesEntries.Count * 2))

    foreach ($entry in $speciesEntries) {
        $sourceSpecies = [int]$entry.National
        $internalSpecies = [int]$entry.Internal
        foreach ($shiny in $false, $true) {
            $frames = Read-Frames -Species $sourceSpecies -Shiny $shiny
            try {
                $colors = [Collections.Generic.List[Drawing.Color]]::new()
                $seen = [Collections.Generic.HashSet[int]]::new()
                foreach ($bitmap in $frames) {
                    for ($y = 0; $y -lt 32; ++$y) {
                        for ($x = 0; $x -lt 32; ++$x) {
                            $color = $bitmap.GetPixel($x, $y)
                            if ($color.A -eq 0) { continue }
                            if ($color.A -ne 255) {
                                throw 'Follower sprites must use binary alpha.'
                            }
                            if ($seen.Add($color.ToArgb())) {
                                $colors.Add($color)
                            }
                        }
                    }
                }
                if ($colors.Count -gt 15) {
                    throw "Species $sourceSpecies needs $($colors.Count) opaque colors; max is 15."
                }

                $paletteIndex =
                    [Collections.Generic.Dictionary[int, int]]::new()
                for ($index = 0; $index -lt $colors.Count; ++$index) {
                    $paletteIndex[$colors[$index].ToArgb()] = $index + 1
                }

                $writer.Write([uint16]$internalSpecies)
                $writer.Write([uint16]([int]$shiny))
                $writer.Write([uint16]0)
                foreach ($color in $colors) {
                    $writer.Write((Convert-Bgr555 $color))
                }
                for ($index = $colors.Count + 1; $index -lt 16; ++$index) {
                    $writer.Write([uint16]0)
                }
                foreach ($bitmap in $frames) {
                    Write-Tiled4Bpp -Writer $writer -Bitmap $bitmap `
                        -PaletteIndex $paletteIndex
                }
            } finally {
                foreach ($bitmap in $frames) { $bitmap.Dispose() }
            }
        }
    }
} finally {
    $writer.Dispose()
}

if (Test-Path -LiteralPath $OutputPath -PathType Leaf) {
    # Windows PowerShell 5.1's overload binder rejects a null backup path.
    # Keep the replacement atomic and put its short-lived backup beside the
    # D:-only generated asset.
    $backupPath = $OutputPath + '.bak'
    if (Test-Path -LiteralPath $backupPath -PathType Leaf) {
        [IO.File]::Delete($backupPath)
    }
    [IO.File]::Replace($temporaryPath, $OutputPath, $backupPath)
    [IO.File]::Delete($backupPath)
} else {
    [IO.File]::Move($temporaryPath, $OutputPath)
}
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutputPath).Hash
Write-Host "Follower sprite pack: $OutputPath"
Write-Host "SHA-256: $hash"
