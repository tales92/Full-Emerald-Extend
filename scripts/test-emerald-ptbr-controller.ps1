[CmdletBinding()]
param(
    [string]$LocalRoot = 'D:\Gen3RecompLocal',
    [string]$BuildDir = '',
    [string]$RomPath = '',
    [string]$BiosPath = '',
    [string]$SaveSource = '',
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'

function Get-FullPath([string]$Path) {
    return [System.IO.Path]::GetFullPath($Path)
}

function Assert-OnDataDrive([string]$Path, [string]$Label) {
    $full = Get-FullPath $Path
    $root = [System.IO.Path]::GetPathRoot($full)
    if ($root -ine 'D:\') {
        throw "$Label deve ficar no disco D: ($full)"
    }
}

$productRoot = Get-FullPath (Join-Path $PSScriptRoot '..')
$LocalRoot = Get-FullPath $LocalRoot
Assert-OnDataDrive $productRoot 'Pasta do Gen3Recomp'
Assert-OnDataDrive $LocalRoot 'LocalRoot'
if (-not $BuildDir) {
    $BuildDir = Join-Path $productRoot 'build'
}
if (-not $RomPath) {
    $RomPath = Join-Path $LocalRoot 'roms\PokemonEmerald-PTBR-Zambrakas.gba'
}
if (-not $BiosPath) {
    $BiosPath = Join-Path $LocalRoot 'bios\gba_bios.bin'
}
if (-not $SaveSource) {
    $SaveSource = Join-Path $LocalRoot 'saves\emerald_ptbr_zambrakas.sav'
}
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $LocalRoot 'logs'
}

$BuildDir = Get-FullPath $BuildDir
$RomPath = Get-FullPath $RomPath
$BiosPath = Get-FullPath $BiosPath
$SaveSource = Get-FullPath $SaveSource
$OutputRoot = Get-FullPath $OutputRoot
Assert-OnDataDrive $BuildDir 'BuildDir'
Assert-OnDataDrive $RomPath 'RomPath'
Assert-OnDataDrive $BiosPath 'BiosPath'
Assert-OnDataDrive $SaveSource 'SaveSource'
Assert-OnDataDrive $OutputRoot 'OutputRoot'

$executable = Join-Path $BuildDir 'Gen3Recomp.exe'
foreach ($required in @($executable, $RomPath, $BiosPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Arquivo obrigatorio nao encontrado: $required"
    }
}

$expectedRomSha1 = '18A2B0ACDBA046C71B8677BB58C9EE7D36F7A91F'
$expectedBiosSha1 = '300C20DF6731A33952DED8C436F7F186D25D3492'
if ((Get-FileHash -Algorithm SHA1 -LiteralPath $RomPath).Hash -ne $expectedRomSha1) {
    throw 'A ROM nao corresponde ao perfil Emerald PT-BR Zambrakas suportado.'
}
if ((Get-FileHash -Algorithm SHA1 -LiteralPath $BiosPath).Hash -ne $expectedBiosSha1) {
    throw 'A BIOS nao corresponde ao dump retail GBA esperado.'
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$runDirectory = Join-Path $OutputRoot ("controller-acceptance-$stamp")
$tempDirectory = Join-Path $runDirectory 'tmp'
Assert-OnDataDrive $runDirectory 'Pasta de resultados'
Assert-OnDataDrive $tempDirectory 'Pasta temporaria'
New-Item -ItemType Directory -Force -Path $tempDirectory | Out-Null

$testSave = Join-Path $runDirectory 'emerald_ptbr_controller_test.sav'
if (Test-Path -LiteralPath $SaveSource -PathType Leaf) {
    Copy-Item -LiteralPath $SaveSource -Destination $testSave
} else {
    Write-Warning 'Save de origem nao encontrado. O teste vai abrir sem copiar seu progresso.'
}

# An explicit --rom makes launcher_seam treat the run as headless and skip the
# launcher, even when --launcher is also present. Seed the launcher's normal
# exe-local caches instead, exactly like run-emerald-ptbr.ps1 does. This lets
# the player select a controller/deadzone without copying ROM or BIOS.
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText(
    (Join-Path $BuildDir 'rom.cfg'),
    ($RomPath + [Environment]::NewLine),
    $utf8NoBom)
[System.IO.File]::WriteAllText(
    (Join-Path $BuildDir 'bios.cfg'),
    ($BiosPath + [Environment]::NewLine),
    $utf8NoBom)

$inputTrace = Join-Path $runDirectory 'input.csv'
$stdoutLog = Join-Path $runDirectory 'stdout.log'
$stderrLog = Join-Path $runDirectory 'stderr.log'

$oldEnvironment = @{}
foreach ($name in @(
    'GBARECOMP_INPUT_RECORD',
    'GBARECOMP_INPUT_REPLAY',
    'LNG_SMOKE_FRAMES',
    'TEMP',
    'TMP',
    'TMPDIR')) {
    $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

Write-Host ''
Write-Host 'TESTE VISUAL DO CONTROLE' -ForegroundColor Cyan
Write-Host 'Deixe o controle conectado antes de continuar.' -ForegroundColor Yellow
Write-Host '1. No launcher, mova a selecao com o D-pad e com o analogico esquerdo.'
Write-Host '2. Use o botao inferior para confirmar e o botao da direita para voltar.'
Write-Host '3. Em CONTROLLER, escolha o nome do seu controle. Comece com deadzone 50%.'
Write-Host '4. Selecione PLAY. No jogo, teste D-pad e analogico nas quatro direcoes.'
Write-Host '5. Pressione A, B, L, R, Start e Select pelo menos uma vez.'
Write-Host '6. Pressione Guide/Home: o menu do jogo deve abrir e aceitar D-pad, A e B.'
Write-Host '7. Se puder, desconecte/reconecte e confirme que nenhum botao fica preso.'
Write-Host '8. Feche pelo X da janela para receber o resumo automatico.'
Write-Host ''

$exitCode = 1
try {
    $env:GBARECOMP_INPUT_RECORD = $inputTrace
    Remove-Item -Path 'Env:GBARECOMP_INPUT_REPLAY' -ErrorAction SilentlyContinue
    Remove-Item -Path 'Env:LNG_SMOKE_FRAMES' -ErrorAction SilentlyContinue
    $env:TEMP = $tempDirectory
    $env:TMP = $tempDirectory
    $env:TMPDIR = $tempDirectory

    $arguments = @(
        '--launcher',
        '--save', $testSave
    )

    # Start-Process keeps SDL's launcher/game windows interactive while
    # capturing native stderr verbatim. Direct PowerShell redirection turns
    # ordinary SDL diagnostics into terminating ErrorRecords under our strict
    # error policy.
    $quotedArguments = foreach ($argument in $arguments) {
        '"' + $argument.Replace('"', '\"') + '"'
    }
    $process = Start-Process -FilePath $executable `
        -ArgumentList $quotedArguments `
        -WorkingDirectory $productRoot `
        -RedirectStandardOutput $stdoutLog `
        -RedirectStandardError $stderrLog `
        -PassThru -Wait
    $exitCode = $process.ExitCode
} finally {
    foreach ($name in $oldEnvironment.Keys) {
        $oldValue = $oldEnvironment[$name]
        if ($null -eq $oldValue) {
            Remove-Item -Path ("Env:$name") -ErrorAction SilentlyContinue
        } else {
            [Environment]::SetEnvironmentVariable($name, $oldValue, 'Process')
        }
    }
}

$buttonNames = @('A', 'B', 'Select', 'Start', 'Right', 'Left', 'Up', 'Down', 'R', 'L')
$seen = New-Object 'bool[]' 10
if (Test-Path -LiteralPath $inputTrace -PathType Leaf) {
    foreach ($line in Get-Content -LiteralPath $inputTrace) {
        if (-not $line -or $line[0] -eq '#') { continue }
        $parts = $line.Split(',')
        if ($parts.Count -ne 2 -or -not $parts[1].StartsWith('0x')) { continue }
        try {
            $keyinput = [Convert]::ToUInt16($parts[1].Substring(2), 16)
        } catch {
            continue
        }
        for ($bit = 0; $bit -lt 10; $bit++) {
            if (($keyinput -band (1 -shl $bit)) -eq 0) {
                $seen[$bit] = $true
            }
        }
    }
}

$observed = @()
$missing = @()
for ($bit = 0; $bit -lt 10; $bit++) {
    if ($seen[$bit]) { $observed += $buttonNames[$bit] }
    else { $missing += $buttonNames[$bit] }
}

$controllerLine = $null
$controllerOpenCount = 0
if (Test-Path -LiteralPath $stderrLog -PathType Leaf) {
    $controllerMatches = @(Select-String -LiteralPath $stderrLog -Pattern 'host_window: controller=')
    $controllerOpenCount = $controllerMatches.Count
    $controllerLine = $controllerMatches | Select-Object -Last 1
}
$coverageLine = $null
foreach ($log in @($stdoutLog, $stderrLog)) {
    if (Test-Path -LiteralPath $log -PathType Leaf) {
        $hit = Select-String -LiteralPath $log -Pattern 'self_heal_coverage=' |
            Select-Object -Last 1
        if ($hit) { $coverageLine = $hit }
    }
}

Write-Host ''
Write-Host 'RESUMO DO TESTE' -ForegroundColor Cyan
Write-Host "Exit code: $exitCode"
if ($controllerLine) {
    Write-Host $controllerLine.Line
    Write-Host "Deteccoes/aberturas do controle: $controllerOpenCount"
} else {
    Write-Host 'CONTROLE NAO DETECTADO pelo SDL.' -ForegroundColor Red
    Write-Host 'Confira se ele estava ligado antes do launcher e envie o stderr.log para diagnostico.'
}
if ($coverageLine) { Write-Host $coverageLine.Line }
Write-Host ("Inputs observados: " + ($(if ($observed.Count) { $observed -join ', ' } else { 'nenhum' })))
if ($missing.Count) {
    Write-Host ("Ainda nao observados: " + ($missing -join ', ')) -ForegroundColor Yellow
}
if ($exitCode -eq 0 -and $controllerLine -and $missing.Count -eq 0) {
    Write-Host 'RESULTADO AUTOMATICO: APROVADO - os 10 inputs do GBA foram observados.' -ForegroundColor Green
} else {
    Write-Host 'RESULTADO AUTOMATICO: INCOMPLETO - veja os avisos acima.' -ForegroundColor Yellow
}
Write-Host 'Confirmacao visual ainda necessaria: analogico, Guide/Home e reconexao.'
Write-Host "Resultados: $runDirectory"

exit $exitCode
