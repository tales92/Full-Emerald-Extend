[CmdletBinding()]
param(
    [string]$LocalRoot = 'D:\Gen3RecompLocal',
    [string]$BuildDir = '',
    [switch]$ContinueSession,
    [switch]$ResetSession
)

$ErrorActionPreference = 'Stop'

function Get-FullPath([string]$Path) {
    return [IO.Path]::GetFullPath($Path)
}

function Assert-OnDataDrive([string]$Path, [string]$Label) {
    $full = Get-FullPath $Path
    if ([IO.Path]::GetPathRoot($full) -ine 'D:\') {
        throw "$Label must stay on D: ($full)"
    }
}

$repositoryRoot = Get-FullPath (Join-Path $PSScriptRoot '..')
if (-not $BuildDir) { $BuildDir = Join-Path $repositoryRoot 'build' }
$BuildDir = Get-FullPath $BuildDir
$LocalRoot = Get-FullPath $LocalRoot
$testRoot = Join-Path $LocalRoot 'follower-test'
$testSave = Join-Path $testRoot 'save\follower-visual-test.sav'
$tempRoot = Join-Path $testRoot 'tmp\visual'
$originalSave = Join-Path $LocalRoot 'saves\emerald_ptbr_zambrakas.sav'
$statePath = Join-Path $LocalRoot 'states\emerald_ptbr_before_first_save.gbas'
$romPath = Join-Path $LocalRoot 'roms\PokemonEmerald-PTBR-Zambrakas.gba'
$biosPath = Join-Path $LocalRoot 'bios\gba_bios.bin'
$executable = Join-Path $BuildDir 'Gen3Recomp-gui.exe'
$modsRoot = Join-Path $BuildDir 'mods'

foreach ($item in @(
    @{ Path = $repositoryRoot; Label = 'Repository' },
    @{ Path = $BuildDir; Label = 'Build' },
    @{ Path = $testRoot; Label = 'Test session' },
    @{ Path = $testSave; Label = 'Test save' },
    @{ Path = $tempRoot; Label = 'Temporary directory' },
    @{ Path = $romPath; Label = 'ROM' },
    @{ Path = $biosPath; Label = 'BIOS' },
    @{ Path = $statePath; Label = 'Savestate' })) {
    Assert-OnDataDrive $item.Path $item.Label
}

foreach ($required in @($executable, $originalSave, $statePath,
                         $romPath, $biosPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required test file is missing: $required"
    }
}
if ((Get-FileHash -Algorithm SHA1 -LiteralPath $romPath).Hash -ne
    '18A2B0ACDBA046C71B8677BB58C9EE7D36F7A91F') {
    throw 'The visual follower test currently requires the verified Emerald ROM.'
}

New-Item -ItemType Directory -Force -Path `
    (Split-Path -Parent $testSave), $tempRoot, $modsRoot | Out-Null
if ($ResetSession -or -not (Test-Path -LiteralPath $testSave -PathType Leaf)) {
    Copy-Item -LiteralPath $originalSave -Destination $testSave -Force
}

$stateToml = @'
format_version = 1

[[package]]
id = "pokemon-emerald.enhancement.follower"
version = "1.1.0"

[[feature]]
package_id = "pokemon-emerald.enhancement.follower"
id = "follower"
enabled = true
'@
[IO.File]::WriteAllText(
    (Join-Path $modsRoot 'state.toml'), $stateToml,
    [Text.UTF8Encoding]::new($false))

$arguments = @(
    '--bios', $biosPath,
    '--rom', $romPath,
    '--save', $testSave,
    '--no-launcher',
    '--no-bios-hle'
)
if (-not $ContinueSession) {
    $arguments += @('--load-state', $statePath)
}

$oldEnvironment = @{}
foreach ($name in @('TEMP', 'TMP', 'TMPDIR')) {
    $oldEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    $env:TEMP = $tempRoot
    $env:TMP = $tempRoot
    $env:TMPDIR = $tempRoot
    Write-Host ''
    Write-Host 'FOLLOWER VISUAL TEST'
    Write-Host '1. Press B to leave the initial save prompt, then leave the truck.'
    Write-Host '2. Play normally until you choose Treecko, Torchic, or Mudkip.'
    Write-Host '3. Finish the first battle and walk one tile in the overworld.'
    Write-Host '4. Confirm direction, two-frame animation, doors, and map changes.'
    Write-Host ''
    Write-Host "Isolated test save: $testSave"
    Write-Host 'Your shiny-hunt save is never passed to the game by this script.'
    Write-Host ''
    Push-Location -LiteralPath $repositoryRoot
    try {
        & $executable @arguments
    } finally {
        Pop-Location
    }
} finally {
    foreach ($name in $oldEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable(
            $name, $oldEnvironment[$name], 'Process')
    }
}
