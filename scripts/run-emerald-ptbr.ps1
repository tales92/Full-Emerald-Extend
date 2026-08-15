[CmdletBinding()]
param(
    [string]$LocalRoot = 'D:\Gen3RecompLocal',
    [string]$BuildDir = '',
    [string]$RomPath = '',
    [string]$BiosPath = '',
    [string]$SavePath = '',
    [switch]$NoLauncher,
    [ValidateRange(0, 1000000)]
    [int]$Frames = 0
)

$ErrorActionPreference = 'Stop'

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Assert-OnDataDrive([string]$Path, [string]$Label) {
    $full = Get-FullPath $Path
    if ([System.IO.Path]::GetPathRoot($full) -ine 'D:\') {
        throw "$Label deve ficar no disco D: ($full)"
    }
}

$productRoot = [System.IO.Path]::GetFullPath(
    (Join-Path -Path $PSScriptRoot -ChildPath '..'))
$LocalRoot = Get-FullPath $LocalRoot
Assert-OnDataDrive $productRoot 'Pasta do Gen3Recomp'
Assert-OnDataDrive $LocalRoot 'LocalRoot'
if (-not $BuildDir) {
    $BuildDir = Join-Path -Path $productRoot -ChildPath 'build'
}
if (-not $RomPath) {
    $RomPath = Join-Path -Path $LocalRoot -ChildPath 'roms\PokemonEmerald-PTBR-Zambrakas.gba'
}
if (-not $BiosPath) {
    $BiosPath = Join-Path -Path $LocalRoot -ChildPath 'bios\gba_bios.bin'
}
if (-not $SavePath) {
    $SavePath = Join-Path -Path $LocalRoot -ChildPath 'saves\emerald_ptbr_zambrakas.sav'
}

$BuildDir = Get-FullPath $BuildDir
$RomPath = Get-FullPath $RomPath
$BiosPath = Get-FullPath $BiosPath
$SavePath = Get-FullPath $SavePath
$tempDirectory = Get-FullPath (Join-Path -Path $LocalRoot -ChildPath 'tmp\runtime')

# The launcher writes its path/config caches beside the executable and the
# runtime may use the process temp directory. Keep every writable location on
# D: even when Windows itself points TEMP/TMP at C:.
Assert-OnDataDrive $BuildDir 'BuildDir'
Assert-OnDataDrive $RomPath 'RomPath'
Assert-OnDataDrive $BiosPath 'BiosPath'
Assert-OnDataDrive $SavePath 'SavePath'
Assert-OnDataDrive $tempDirectory 'Diretorio temporario'
New-Item -ItemType Directory -Force -Path $tempDirectory | Out-Null

$executable = Join-Path -Path $BuildDir -ChildPath 'Gen3Recomp.exe'
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Gen3Recomp.exe nao encontrado em: $executable"
}
if (-not (Test-Path -LiteralPath $RomPath -PathType Leaf)) {
    throw "ROM nao encontrada em: $RomPath"
}

$expectedRomSha1 = '18A2B0ACDBA046C71B8677BB58C9EE7D36F7A91F'
$actualRomSha1 = (Get-FileHash -Algorithm SHA1 -LiteralPath $RomPath).Hash
if ($actualRomSha1 -ne $expectedRomSha1) {
    throw "ROM recusada: SHA-1 $actualRomSha1; esperado $expectedRomSha1"
}

$saveDirectory = Split-Path -Parent $SavePath
New-Item -ItemType Directory -Force -Path $saveDirectory | Out-Null

# Seed the launcher's remembered paths without copying either copyrighted file
# into the source tree or build output.
[System.IO.File]::WriteAllText(
    (Join-Path -Path $BuildDir -ChildPath 'rom.cfg'),
    ([System.IO.Path]::GetFullPath($RomPath) + [Environment]::NewLine))

$biosAvailable = Test-Path -LiteralPath $BiosPath -PathType Leaf
if ($biosAvailable) {
    $expectedBiosSha1 = '300C20DF6731A33952DED8C436F7F186D25D3492'
    $actualBiosSha1 = (Get-FileHash -Algorithm SHA1 -LiteralPath $BiosPath).Hash
    if ($actualBiosSha1 -ne $expectedBiosSha1) {
        throw "BIOS recusado: SHA-1 $actualBiosSha1; esperado $expectedBiosSha1"
    }
    [System.IO.File]::WriteAllText(
        (Join-Path -Path $BuildDir -ChildPath 'bios.cfg'),
        ([System.IO.Path]::GetFullPath($BiosPath) + [Environment]::NewLine))
} elseif ($NoLauncher -or $Frames -gt 0) {
    throw "BIOS GBA nao encontrado em: $BiosPath. Forneca seu dump legitimo de 16 KiB."
} else {
    Write-Warning "BIOS nao encontrado em $BiosPath; selecione seu dump legitimo no launcher."
}

$runArguments = @('--save', [System.IO.Path]::GetFullPath($SavePath))
if ($Frames -gt 0) {
    $runArguments += @(
        '--bios', [System.IO.Path]::GetFullPath($BiosPath),
        '--rom', [System.IO.Path]::GetFullPath($RomPath),
        '--frames', $Frames.ToString(),
        '--no-window'
    )
} elseif ($NoLauncher) {
    $runArguments += @(
        '--bios', [System.IO.Path]::GetFullPath($BiosPath),
        '--rom', [System.IO.Path]::GetFullPath($RomPath),
        '--no-launcher'
    )
} else {
    $runArguments += '--launcher'
}

$oldEnvironment = @{}
foreach ($name in @('TEMP', 'TMP', 'TMPDIR')) {
    $oldEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, 'Process')
}

Push-Location -LiteralPath $productRoot
try {
    $env:TEMP = $tempDirectory
    $env:TMP = $tempDirectory
    $env:TMPDIR = $tempDirectory
    & $executable @runArguments
    exit $LASTEXITCODE
} finally {
    foreach ($name in $oldEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable(
            $name, $oldEnvironment[$name], 'Process')
    }
    Pop-Location
}
