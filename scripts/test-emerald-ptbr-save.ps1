[CmdletBinding()]
param(
    [string]$LocalRoot = 'D:\Gen3RecompLocal',
    [string]$BuildDir = '',
    [string]$RomPath = '',
    [string]$BiosPath = '',
    [string]$StatePath = '',
    [UInt64]$StateFrame = 6859,
    [string]$OutputRoot = '',
    [ValidateRange(60, 10000)]
    [int]$Frames = 600
)

$ErrorActionPreference = 'Stop'

$productRoot = [IO.Path]::GetFullPath(
    (Join-Path -Path $PSScriptRoot -ChildPath '..'))
if (-not $BuildDir) {
    $BuildDir = Join-Path -Path $productRoot -ChildPath 'build'
}
if (-not $RomPath) {
    $RomPath = Join-Path -Path $LocalRoot -ChildPath `
        'roms\PokemonEmerald-PTBR-Zambrakas.gba'
}
if (-not $BiosPath) {
    $BiosPath = Join-Path -Path $LocalRoot -ChildPath 'bios\gba_bios.bin'
}
if (-not $StatePath) {
    $StatePath = Join-Path -Path $LocalRoot -ChildPath `
        'states\emerald_ptbr_before_first_save.gbas'
}
if (-not $OutputRoot) {
    $OutputRoot = Join-Path -Path $LocalRoot -ChildPath 'logs'
}

$executable = [IO.Path]::GetFullPath(
    (Join-Path -Path $BuildDir -ChildPath 'Gen3Recomp.exe'))
$RomPath = [IO.Path]::GetFullPath($RomPath)
$BiosPath = [IO.Path]::GetFullPath($BiosPath)
$StatePath = [IO.Path]::GetFullPath($StatePath)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)

# This project deliberately keeps all generated artifacts off the user's C:
# drive. ROM, BIOS, and state inputs are read-only; the output root is the only
# caller-controlled write location.
if ([IO.Path]::GetPathRoot($OutputRoot) -ieq 'C:\') {
    throw "OutputRoot deve ficar no D:, nao no C:: $OutputRoot"
}

foreach ($inputPath in @($executable, $RomPath, $BiosPath, $StatePath)) {
    if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) {
        throw "Arquivo obrigatorio nao encontrado: $inputPath"
    }
}

$expectedRomSha1 = '18A2B0ACDBA046C71B8677BB58C9EE7D36F7A91F'
$expectedBiosSha1 = '300C20DF6731A33952DED8C436F7F186D25D3492'
$actualRomSha1 = (Get-FileHash -Algorithm SHA1 -LiteralPath $RomPath).Hash
$actualBiosSha1 = (Get-FileHash -Algorithm SHA1 -LiteralPath $BiosPath).Hash
if ($actualRomSha1 -ne $expectedRomSha1) {
    throw "ROM PT-BR recusada: SHA-1 $actualRomSha1; esperado $expectedRomSha1"
}
if ($actualBiosSha1 -ne $expectedBiosSha1) {
    throw "BIOS recusado: SHA-1 $actualBiosSha1; esperado $expectedBiosSha1"
}

$stateBytes = [IO.File]::ReadAllBytes($StatePath)
if ($stateBytes.Length -lt 4 -or
    [Text.Encoding]::ASCII.GetString($stateBytes, 0, 4) -ne 'GBAS') {
    throw "Savestate invalido (cabecalho GBAS ausente): $StatePath"
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = Join-Path -Path $OutputRoot -ChildPath "save-acceptance-$stamp"
$tempDir = Join-Path -Path $runDir -ChildPath 'tmp'
New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

$tracePath = Join-Path -Path $runDir -ChildPath 'confirm-save.keys'
$savePath = Join-Path -Path $runDir -ChildPath 'emerald_ptbr_acceptance.sav'
$logPath = Join-Path -Path $runDir -ChildPath 'runtime.log'
$releaseFrame = $StateFrame + 6
[IO.File]::WriteAllLines($tracePath, @(
    '# gbarecomp-keyinput-v1'
    '# frame,keyinput_active_low'
    ("{0},0x03FE" -f $StateFrame)
    ("{0},0x03FF" -f $releaseFrame)
), [Text.UTF8Encoding]::new($false))

$environmentNames = @(
    'TEMP',
    'TMP',
    'TMPDIR',
    'GBARECOMP_STRICT_STATIC',
    'GBARECOMP_INPUT_REPLAY'
)
$previousEnvironment = @{}
foreach ($name in $environmentNames) {
    $previousEnvironment[$name] =
        [Environment]::GetEnvironmentVariable($name, 'Process')
}

$arguments = @(
    '--bios', $BiosPath,
    '--rom', $RomPath,
    '--save', $savePath,
    '--load-state', $StatePath,
    '--frames', $Frames.ToString(),
    '--no-window',
    '--no-launcher',
    '--no-bios-hle'
)

try {
    $env:TEMP = $tempDir
    $env:TMP = $tempDir
    $env:TMPDIR = $tempDir
    $env:GBARECOMP_STRICT_STATIC = '1'
    $env:GBARECOMP_INPUT_REPLAY = $tracePath

    Push-Location -LiteralPath $productRoot
    try {
        # Windows PowerShell 5.1 wraps native stderr as NativeCommandError when
        # ErrorActionPreference is Stop. The runtime legitimately writes its
        # idle-loop summary to stderr, so collect both streams without turning
        # that diagnostic into a PowerShell exception; the native exit code and
        # strict-static log gates below remain authoritative.
        $savedErrorActionPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $runtimeLines = @(& $executable @arguments 2>&1 | ForEach-Object {
                $_.ToString()
            })
            $runtimeExitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $savedErrorActionPreference
        }
    } finally {
        Pop-Location
    }
} finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable(
            $name, $previousEnvironment[$name], 'Process')
    }
}

[IO.File]::WriteAllLines(
    $logPath, $runtimeLines, [Text.UTF8Encoding]::new($false))
$runtimeLog = [string]::Join([Environment]::NewLine, $runtimeLines)

if ($runtimeExitCode -ne 0) {
    throw "Runtime retornou $runtimeExitCode. Consulte $logPath"
}
if ($runtimeLog -match 'STRICT_STATIC dispatch miss') {
    throw "O smoke test encontrou dispatch miss. Consulte $logPath"
}
if ($runtimeLog -notmatch
    'self_heal_coverage=FULLY_STATIC dispatch_misses=0 interpreted_insns=0') {
    throw "Cobertura static esperada nao foi confirmada. Consulte $logPath"
}
if ($runtimeLog -notmatch 'save_flushed') {
    throw "O runtime nao confirmou o flush do save. Consulte $logPath"
}
if ($runtimeLog -notmatch ("savestate_loaded .* frame={0}" -f $StateFrame)) {
    throw "O frame do savestate nao confere com StateFrame=$StateFrame."
}
if (-not (Test-Path -LiteralPath $savePath -PathType Leaf)) {
    throw "Arquivo de save nao foi criado: $savePath"
}

$saveBytes = [IO.File]::ReadAllBytes($savePath)
if ($saveBytes.Length -ne 0x20000) {
    throw "Save tem $($saveBytes.Length) bytes; esperado 131072."
}
$nonFF = 0
foreach ($value in $saveBytes) {
    if ($value -ne 0xFF) { ++$nonFF }
}

$sectionIds = @()
$saveIndexes = @()
for ($offset = 0; $offset -lt $saveBytes.Length; $offset += 0x1000) {
    if ([BitConverter]::ToUInt32($saveBytes, $offset + 0xFF8) -eq
        0x08012025) {
        $sectionIds += [BitConverter]::ToUInt16($saveBytes, $offset + 0xFF4)
        $saveIndexes += [BitConverter]::ToUInt32($saveBytes, $offset + 0xFFC)
    }
}
$uniqueSectionIds = @($sectionIds | Sort-Object -Unique)
$expectedIds = 0..13
if ($sectionIds.Count -ne 14 -or
    [string]::Join(',', $uniqueSectionIds) -ne
    [string]::Join(',', $expectedIds)) {
    throw "Footers Emerald invalidos: ids=$([string]::Join(',', $sectionIds))"
}
if ($nonFF -eq 0) {
    throw 'Save permaneceu inteiramente preenchido com 0xFF.'
}
if (Test-Path -LiteralPath ($savePath + '.tmp')) {
    throw "Arquivo temporario residual encontrado: $savePath.tmp"
}

$result = [pscustomobject]@{
    Success = $true
    RuntimeExitCode = $runtimeExitCode
    Coverage = 'FULLY_STATIC'
    DispatchMisses = 0
    InterpretedInstructions = 0
    SaveBytes = $saveBytes.Length
    NonFFBytes = $nonFF
    ValidSectionFooters = $sectionIds.Count
    SectionIds = [string]::Join(',', $uniqueSectionIds)
    SaveIndexes = [string]::Join(',', @($saveIndexes | Sort-Object -Unique))
    SaveSHA256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $savePath).Hash
    RunDirectory = $runDir
    SavePath = $savePath
    RuntimeLog = $logPath
}

$result
