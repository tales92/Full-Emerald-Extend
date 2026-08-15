<#
.SYNOPSIS
Builds and audits the portable Full Emerald Windows package.

.DESCRIPTION
The release is deliberately assembled from an allowlist. It contains the
native executable, its four non-system DLL dependencies, launcher assets, and
license/readme files only. User-owned ROMs and BIOS images, saves, states,
generated guest source, caches, and logs are never copied.

All writable paths (build, TEMP, staging, ZIP audit, and smoke-test output)
must resolve to D:. The script refuses to run if any of them resolve to C: or
another drive.

.EXAMPLE
  powershell -NoProfile -File tools\make_release.ps1 -Version 0.1.0-dev

.EXAMPLE
  powershell -NoProfile -File tools\make_release.ps1 `
      -Version 0.1.0-dev -BuildDir build -SkipBuild
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9A-Za-z][0-9A-Za-z._-]*$')]
    [string]$Version,

    [string]$BuildDir = 'build',
    [string]$ReleaseDir = 'release-stage',
    [string]$ToolchainBin = 'D:\Gen1recomp\.toolchains\msys64\mingw64\bin',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Resolve-RepositoryPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$RepositoryRoot
    )

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $RepositoryRoot $Path))
}

function Assert-DataDrivePath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $full = [System.IO.Path]::GetFullPath($Path)
    $drive = [System.IO.Path]::GetPathRoot($full)
    if ($drive -ine 'D:\') {
        throw "$Label must resolve to D: (resolved '$full'). Nothing is written to C:."
    }
}

function Assert-ChildPath {
    param(
        [Parameter(Mandatory = $true)][string]$Child,
        [Parameter(Mandatory = $true)][string]$Parent,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $childFull = [System.IO.Path]::GetFullPath($Child)
    $parentFull = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    if (-not $childFull.StartsWith(
            $parentFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label escaped its intended root: '$childFull' is not under '$parentFull'."
    }
}

function Get-RelativeFileList {
    param([Parameter(Mandatory = $true)][string]$Directory)

    $prefix = [System.IO.Path]::GetFullPath($Directory).TrimEnd('\') + '\'
    return @(
        Get-ChildItem -LiteralPath $Directory -Recurse -File |
            ForEach-Object { $_.FullName.Substring($prefix.Length) } |
            Sort-Object
    )
}

function Assert-NoForbiddenPayload {
    param([Parameter(Mandatory = $true)][string]$Directory)

    $forbiddenExtensions = @(
        '.gba', '.gb', '.gbc', '.agb', '.bin',
        '.sav', '.srm', '.fla', '.flash',
        '.gbas', '.state', '.state0', '.state1', '.state2', '.state3',
        '.state4', '.state5', '.state6', '.state7', '.state8', '.state9',
        '.log', '.csv', '.json',
        '.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.inc'
    )
    $forbiddenLeafNames = @(
        'rom.cfg', 'bios.cfg', 'config.ini', 'keybinds.ini',
        'recomp_coverage_bpee.json'
    )
    $forbiddenDirectoryNames = @(
        'rom', 'roms', 'bios', 'saves', 'states', 'logs', 'generated',
        'recomp_cache'
    )

    $directoryPrefix = [System.IO.Path]::GetFullPath($Directory).TrimEnd('\') + '\'
    $bad = @()
    foreach ($file in Get-ChildItem -LiteralPath $Directory -Recurse -File) {
        $relative = $file.FullName.Substring($directoryPrefix.Length)
        $segments = @($relative -split '[\\/]')
        $directories = if ($segments.Count -gt 1) {
            @($segments[0..($segments.Count - 2)])
        } else {
            @()
        }
        if ($forbiddenExtensions -contains $file.Extension.ToLowerInvariant() -or
            $forbiddenLeafNames -contains $file.Name.ToLowerInvariant() -or
            @($directories | Where-Object {
                $forbiddenDirectoryNames -contains $_.ToLowerInvariant()
            }).Count -gt 0) {
            $bad += $relative
        }
    }

    if ($bad.Count -gt 0) {
        throw "Forbidden release payload detected: $($bad -join ', ')"
    }
}

function Assert-ExactFileSet {
    param(
        [Parameter(Mandatory = $true)][string]$Directory,
        [Parameter(Mandatory = $true)][string[]]$Expected
    )

    $actual = @(Get-RelativeFileList -Directory $Directory)
    $missing = @($Expected | Where-Object { $_ -notin $actual })
    $unexpected = @($actual | Where-Object { $_ -notin $Expected })
    if ($missing.Count -gt 0 -or $unexpected.Count -gt 0) {
        throw ("Release allowlist mismatch. Missing: [{0}] Unexpected: [{1}]" -f
            ($missing -join ', '), ($unexpected -join ', '))
    }
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildPath = Resolve-RepositoryPath -Path $BuildDir -RepositoryRoot $repositoryRoot
$releaseRoot = Resolve-RepositoryPath -Path $ReleaseDir -RepositoryRoot $repositoryRoot
$toolchainPath = Resolve-RepositoryPath -Path $ToolchainBin -RepositoryRoot $repositoryRoot

Assert-DataDrivePath -Path $repositoryRoot -Label 'Repository root'
Assert-DataDrivePath -Path $buildPath -Label 'Build directory'
Assert-DataDrivePath -Path $releaseRoot -Label 'Release directory'
Assert-DataDrivePath -Path $toolchainPath -Label 'MinGW toolchain'

$buildTemp = Join-Path $buildPath 'package-tmp'
$auditRoot = Join-Path $buildPath 'package-acceptance'
Assert-DataDrivePath -Path $buildTemp -Label 'Package TEMP directory'
Assert-DataDrivePath -Path $auditRoot -Label 'Package audit directory'
New-Item -ItemType Directory -Force -Path $buildTemp | Out-Null
New-Item -ItemType Directory -Force -Path $auditRoot | Out-Null
$env:TEMP = $buildTemp
$env:TMP = $buildTemp
$env:TMPDIR = $buildTemp
$env:PATH = "$toolchainPath;$env:PATH"

$cmake = Join-Path $toolchainPath 'cmake.exe'
$ninja = Join-Path $toolchainPath 'ninja.exe'
$cCompiler = Join-Path $toolchainPath 'cc.exe'
$cxxCompiler = Join-Path $toolchainPath 'c++.exe'
$strip = Join-Path $toolchainPath 'strip.exe'
$objdump = Join-Path $toolchainPath 'objdump.exe'
foreach ($tool in @($cmake, $ninja, $cCompiler, $cxxCompiler, $strip, $objdump)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required D: tool is missing: $tool"
    }
}

$cmakeCache = Join-Path $buildPath 'CMakeCache.txt'
if (-not $SkipBuild) {
    if (-not (Test-Path -LiteralPath $cmakeCache -PathType Leaf)) {
        $sdl2Dir = Join-Path (Split-Path -Parent $toolchainPath) 'lib\cmake\SDL2'
        & $cmake -S $repositoryRoot -B $buildPath -G Ninja `
            "-DCMAKE_C_COMPILER=$cCompiler" `
            "-DCMAKE_CXX_COMPILER=$cxxCompiler" `
            "-DCMAKE_MAKE_PROGRAM=$ninja" `
            '-DCMAKE_BUILD_TYPE=Release' `
            '-DGBARECOMP_BUILD_ORACLE=OFF' `
            '-DGBAGAME_RECOMP_UI=ON' `
            "-DSDL2_DIR=$sdl2Dir"
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configure failed with exit code $LASTEXITCODE."
        }
    }

    & $cmake --build $buildPath --target Gen3Recomp -j 2
    if ($LASTEXITCODE -ne 0) {
        throw "Gen3Recomp build failed with exit code $LASTEXITCODE."
    }
} elseif (-not (Test-Path -LiteralPath $cmakeCache -PathType Leaf)) {
    throw "-SkipBuild requires an existing CMake cache: $cmakeCache"
}

$cacheText = Get-Content -LiteralPath $cmakeCache -Raw
if ($cacheText -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=Release\s*$') {
    throw 'Refusing to package a non-Release build.'
}
if ($cacheText -match '(?m)^CMAKE_CXX_COMPILER:FILEPATH=(.+)$') {
    Assert-DataDrivePath -Path $Matches[1].Trim() -Label 'Configured C++ compiler'
}

$builtExe = Join-Path $buildPath 'Gen3Recomp.exe'
if (-not (Test-Path -LiteralPath $builtExe -PathType Leaf)) {
    throw "Expected executable is missing: $builtExe"
}
$builtGuiExe = Join-Path $buildPath 'Gen3Recomp-gui.exe'
if (-not (Test-Path -LiteralPath $builtGuiExe -PathType Leaf)) {
    throw ("Expected no-console product executable is missing: $builtGuiExe. " +
        'Build Gen3Recomp with GEN3RECOMP_BUILD_WINDOWS_GUI_COPY=ON.')
}

$stageName = "Full-Emerald-windows-x64-v$Version"
$stagePath = Join-Path $releaseRoot $stageName
$zipPath = Join-Path $releaseRoot "$stageName.zip"
$auditPath = Join-Path $auditRoot $stageName
foreach ($path in @($stagePath, $auditPath)) {
    Assert-ChildPath -Child $path -Parent $(if ($path -eq $stagePath) {
        $releaseRoot
    } else {
        $auditRoot
    }) -Label 'Disposable package directory'
    Assert-DataDrivePath -Path $path -Label 'Disposable package directory'
}

New-Item -ItemType Directory -Force -Path $releaseRoot | Out-Null
if (Test-Path -LiteralPath $stagePath) {
    Remove-Item -LiteralPath $stagePath -Recurse -Force
}
if (Test-Path -LiteralPath $zipPath) {
    Assert-ChildPath -Child $zipPath -Parent $releaseRoot -Label 'Release ZIP'
    Remove-Item -LiteralPath $zipPath -Force
}
if (Test-Path -LiteralPath $auditPath) {
    Remove-Item -LiteralPath $auditPath -Recurse -Force
}
New-Item -ItemType Directory -Path $stagePath | Out-Null

$stagedExe = Join-Path $stagePath 'Full Emerald.exe'
$stagedConsoleExe = Join-Path $stagePath 'Full Emerald Console.exe'
Copy-Item -LiteralPath $builtGuiExe -Destination $stagedExe
Copy-Item -LiteralPath $builtExe -Destination $stagedConsoleExe
foreach ($executable in @($stagedExe, $stagedConsoleExe)) {
    & $strip $executable
    if ($LASTEXITCODE -ne 0) {
        throw "strip failed for $executable with exit code $LASTEXITCODE."
    }
}

$runtimeDlls = @(
    'SDL2.dll',
    'libgcc_s_seh-1.dll',
    'libstdc++-6.dll',
    'libwinpthread-1.dll'
)
foreach ($dll in $runtimeDlls) {
    $source = Join-Path $buildPath $dll
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        $source = Join-Path $toolchainPath $dll
    }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Runtime dependency is missing: $dll"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $stagePath $dll)
}

$assetFiles = @(
    'fonts\LatoLatin-Bold.ttf',
    'fonts\LatoLatin-Regular.ttf',
    'fonts\NotoSansSymbols2-Regular.ttf',
    'fonts\OpenMoji-black-glyf.ttf',
    'img\brand_mark.tga',
    'img\full_emerald_logo.png',
    'img\pad_gba.tga',
    'img\verdict_bad.tga',
    'img\verdict_none.tga',
    'img\verdict_ok.tga',
    'img\verdict_warn.tga'
)
$optionalAssetFiles = @(
    # Captured or supplied locally by the user; never committed publicly.
    'img\boxart_emerald.tga',
    'img\emerald_intro_loop.gif'
)
foreach ($relative in $optionalAssetFiles) {
    if (Test-Path -LiteralPath (Join-Path (Join-Path $buildPath 'assets') $relative) `
            -PathType Leaf) {
        $assetFiles += $relative
    }
}
foreach ($relative in $assetFiles) {
    $source = Join-Path (Join-Path $buildPath 'assets') $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required launcher asset is missing: $source"
    }
    $destination = Join-Path (Join-Path $stagePath 'assets') $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) |
        Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
}

# Ship the audited package only when the user generated its local sprite pack.
# The trusted implementation is statically linked, but the feature remains
# disabled by default and the public source tree does not redistribute art.
$candidatePreloadedModFiles = @(
    'packages\pokemon-emerald.enhancement.follower\1.1.0\manifest.toml',
    'packages\pokemon-emerald.enhancement.follower\1.1.0\assets\follower_emerald.g3fs'
)
$presentPreloadedModFiles = @($candidatePreloadedModFiles | Where-Object {
    Test-Path -LiteralPath (Join-Path (Join-Path $buildPath 'mods') $_) `
        -PathType Leaf
})
if ($presentPreloadedModFiles.Count -ne 0 -and
    $presentPreloadedModFiles.Count -ne $candidatePreloadedModFiles.Count) {
    throw 'The optional Pokemon Follower package is incomplete in the build directory.'
}
$preloadedModFiles = if ($presentPreloadedModFiles.Count -eq
    $candidatePreloadedModFiles.Count) {
    $candidatePreloadedModFiles
} else {
    @()
}
foreach ($relative in $preloadedModFiles) {
    $source = Join-Path (Join-Path $buildPath 'mods') $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required preloaded mod asset is missing: $source"
    }
    $destination = Join-Path (Join-Path $stagePath 'mods') $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) |
        Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
}

$licenseCopies = @(
    @('gbarecomp\LICENSE', 'licenses\GBARECOMP-LICENSE.txt'),
    @('gbarecomp\THIRD_PARTY_ATTRIBUTION.md',
      'licenses\THIRD_PARTY_ATTRIBUTION.md'),
    @('recomp-ui\src\third_party\imgui\LICENSE.txt',
      'licenses\IMGUI-LICENSE.txt'),
    @('recomp-ui\assets\common\fonts\NOTICE.md',
      'licenses\FONT-NOTICE.md')
)
foreach ($copy in $licenseCopies) {
    $source = Join-Path $repositoryRoot $copy[0]
    $destination = Join-Path $stagePath $copy[1]
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) |
        Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
}

$toolchainRoot = Split-Path -Parent $toolchainPath
$toolchainLicenses = @(
    @('share\licenses\SDL2\LICENSE.txt', 'licenses\SDL2-LICENSE.txt'),
    @('share\licenses\gcc-libs\COPYING.RUNTIME',
      'licenses\GCC-RUNTIME-LIBRARY-EXCEPTION.txt'),
    @('share\licenses\gcc-libs\COPYING.LIB',
      'licenses\LGPL-2.1.txt')
)
foreach ($copy in $toolchainLicenses) {
    $source = Join-Path $toolchainRoot $copy[0]
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required runtime license is missing: $source"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $stagePath $copy[1])
}

$packageReadme = @'
# Full Emerald 2.0 (Windows x64)

Este e o pacote portatil da recompilacao nativa de Pokemon Emerald. Ele nao e
um emulador e nao inclui o arquivo da ROM nem o BIOS.

## Como iniciar

1. Extraia a pasta inteira em qualquer pasta com espaco livre e permissao de escrita.
2. Execute `Full Emerald.exe`.
3. No primeiro inicio, selecione o seu dump legitimo da revisao PT-BR
   atualmente suportada e o seu dump legitimo do BIOS do Game Boy Advance.

`Full Emerald.exe` e o aplicativo normal para jogar e nao abre uma janela
preta de terminal. `Full Emerald Console.exe` executa exatamente o mesmo jogo com um
console tecnico; use essa versao em PowerShell, testes headless ou ao capturar
stdout/stderr (`--help`, `--frames`, `--no-window`, `--tcp`, etc.).

O programa confere a identidade dos dois arquivos antes de iniciar. O controle
pode navegar pelo launcher e jogar; teclado e controle funcionam juntos.
Por padrao, segure o gatilho direito (Xbox RT / 8BitDo R2) para acelerar e
solte para voltar imediatamente a velocidade normal. O atalho e configuravel.

Na Home, escolha de 1 a 12 telas simultaneas. Todas recebem o mesmo controle e
avancam juntas; cada tela mantem save, savestate, estado de mods e semente
aleatoria independentes. Durante uma partida normal, pressione Home/Guide no
controle para abrir o menu e escolha Multitelas para continuar daquele ponto em
duas runs. No compositor, o mesmo menu adiciona ou remove telas ao vivo.
Ctrl+R reinicia a rodada inteira com novas sementes, F11 adiciona uma tela e
F12 remove a ultima. Todos os tres atalhos podem ser associados ao teclado ou
ao controle. O X no canto de cada bloco fecha somente aquela tela, e a
opacidade da numeracao tambem e configuravel.

O menu Home/Guide tambem agrupa o slot, Save state e Load state em Save states.
No editor de controles, uma captura expira em 10 segundos; Esc ou clique direito
cancela, e Delete, Backspace ou clique do meio remove o bind atual.

## Conteudo que nao acompanha este pacote

- ROM ou patch;
- BIOS do Game Boy Advance;
- save, savestate ou cache de recompilacao;
- codigo-fonte gerado da ROM;
- configuracao, caminho local ou log do computador de desenvolvimento.

Na primeira execucao, arquivos pessoais como `rom.cfg`, `bios.cfg`,
`config.ini`, `keybinds.ini`, `mods/` e caches podem ser criados ao lado do
executavel. O save padrao fica ao lado da ROM selecionada.

Quando montado com o pack local de sprites, o pacote tambem inclui o mod
`Pokemon Follower`, desativado por padrao. Ative-o na pagina Mods. A arte do mod
nao faz parte do codigo-fonte publico e deve ser gerada a partir de recursos que
o usuario tenha permissao para usar.

Consulte `licenses/` para os avisos de software e fontes de terceiros.
'@
Set-Content -LiteralPath (Join-Path $stagePath 'README.md') `
    -Value $packageReadme -Encoding UTF8

$expectedBeforeManifest = @(
    'Full Emerald.exe',
    'Full Emerald Console.exe',
    'SDL2.dll',
    'libgcc_s_seh-1.dll',
    'libstdc++-6.dll',
    'libwinpthread-1.dll',
    'README.md',
    'licenses\GBARECOMP-LICENSE.txt',
    'licenses\THIRD_PARTY_ATTRIBUTION.md',
    'licenses\IMGUI-LICENSE.txt',
    'licenses\FONT-NOTICE.md',
    'licenses\SDL2-LICENSE.txt',
    'licenses\GCC-RUNTIME-LIBRARY-EXCEPTION.txt',
    'licenses\LGPL-2.1.txt'
) + @($assetFiles | ForEach-Object { "assets\$_" }) +
    @($preloadedModFiles | ForEach-Object { "mods\$_" })

Assert-NoForbiddenPayload -Directory $stagePath
Assert-ExactFileSet -Directory $stagePath -Expected $expectedBeforeManifest

# Audit the PE contract before packaging. The normal product must be Windows
# GUI subsystem (2), while the technical twin stays Windows CUI (3). Both must
# retain the exact same mainCRTStartup address: the GUI copy changes only the
# PE header and does not introduce SDL_main/WinMain argument handling.
function Get-PeResourceTypeIds {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [System.IO.File]::Open(
        $Path,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::Read)
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 64) {
            throw "PE file is too small: $Path"
        }

        if ($reader.ReadUInt16() -ne 0x5a4d) {
            throw "PE DOS signature is missing: $Path"
        }
        $stream.Position = 0x3c
        $peOffset = $reader.ReadUInt32()
        if ($peOffset + 24 -gt $stream.Length) {
            throw "PE header is outside the file: $Path"
        }

        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "PE signature is missing: $Path"
        }
        [void]$reader.ReadUInt16() # Machine
        $sectionCount = $reader.ReadUInt16()
        $stream.Position += 12 # timestamp, symbol table pointer/count
        $optionalHeaderSize = $reader.ReadUInt16()
        [void]$reader.ReadUInt16() # Characteristics
        $optionalHeaderOffset = $stream.Position
        $magic = $reader.ReadUInt16()
        $dataDirectoryOffset = if ($magic -eq 0x10b) {
            96 # PE32
        } elseif ($magic -eq 0x20b) {
            112 # PE32+
        } else {
            throw ("Unsupported PE optional-header magic 0x{0:x}: {1}" -f
                $magic, $Path)
        }
        if ($optionalHeaderSize -lt $dataDirectoryOffset + 24) {
            throw "PE optional header has no resource directory: $Path"
        }

        # IMAGE_DIRECTORY_ENTRY_RESOURCE is data-directory index 2.
        $stream.Position = $optionalHeaderOffset + $dataDirectoryOffset + 16
        $resourceRva = $reader.ReadUInt32()
        $resourceSize = $reader.ReadUInt32()
        if ($resourceRva -eq 0 -or $resourceSize -eq 0) {
            return @()
        }

        $sectionTableOffset = $optionalHeaderOffset + $optionalHeaderSize
        $resourceOffset = $null
        for ($index = 0; $index -lt $sectionCount; ++$index) {
            $sectionOffset = $sectionTableOffset + (40 * $index)
            if ($sectionOffset + 40 -gt $stream.Length) {
                throw "PE section table is truncated: $Path"
            }
            $stream.Position = $sectionOffset + 8
            $virtualSize = $reader.ReadUInt32()
            $virtualAddress = $reader.ReadUInt32()
            $rawSize = $reader.ReadUInt32()
            $rawOffset = $reader.ReadUInt32()
            $mappedSize = [Math]::Max(
                [uint64]$virtualSize, [uint64]$rawSize)
            if ([uint64]$resourceRva -ge [uint64]$virtualAddress -and
                [uint64]$resourceRva -lt
                    ([uint64]$virtualAddress + $mappedSize)) {
                $resourceOffset = [uint64]$rawOffset +
                    ([uint64]$resourceRva - [uint64]$virtualAddress)
                break
            }
        }
        if ($null -eq $resourceOffset -or
            $resourceOffset + 16 -gt [uint64]$stream.Length) {
            throw "PE resource directory is not mapped by a section: $Path"
        }

        # The root IMAGE_RESOURCE_DIRECTORY entries are resource types. Numeric
        # entries 3 and 14 are RT_ICON and RT_GROUP_ICON respectively.
        $stream.Position = [int64]$resourceOffset + 12
        $namedEntryCount = $reader.ReadUInt16()
        $idEntryCount = $reader.ReadUInt16()
        $entryCount = [int]$namedEntryCount + [int]$idEntryCount
        if ($resourceOffset + 16 + (8 * [uint64]$entryCount) -gt
            [uint64]$stream.Length) {
            throw "PE resource type table is truncated: $Path"
        }

        $typeIds = @()
        for ($index = 0; $index -lt $entryCount; ++$index) {
            $stream.Position = [int64]$resourceOffset + 16 + (8 * $index)
            $nameOrId = $reader.ReadUInt32()
            if (($nameOrId -band 0x80000000) -eq 0) {
                $typeIds += [int]($nameOrId -band 0x0000ffff)
            }
        }
        return @($typeIds | Sort-Object -Unique)
    } finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Get-PeInspection {
    param([Parameter(Mandatory = $true)][string]$Path)

    $lines = @(& $objdump -p $Path)
    if ($LASTEXITCODE -ne 0) {
        throw "objdump failed for $Path with exit code $LASTEXITCODE."
    }
    $text = [string]::Join("`n", $lines)
    $subsystemMatch = [regex]::Match(
        $text, 'Subsystem\s+([0-9A-Fa-f]+)\s+\(([^)]+)\)')
    $entryMatch = [regex]::Match(
        $text, 'AddressOfEntryPoint\s+([0-9A-Fa-f]+)')
    if (-not $subsystemMatch.Success -or -not $entryMatch.Success) {
        throw "Could not parse PE subsystem/entry point for $Path."
    }
    $imports = @(
        $lines |
            Select-String 'DLL Name:\s*(\S+)' |
            ForEach-Object {
                $_.Matches[0].Groups[1].Value.ToLowerInvariant()
            } |
            Sort-Object -Unique
    )
    return [pscustomobject]@{
        Path = $Path
        Subsystem = [Convert]::ToUInt32($subsystemMatch.Groups[1].Value, 16)
        SubsystemName = $subsystemMatch.Groups[2].Value
        EntryPoint = $entryMatch.Groups[1].Value.ToLowerInvariant()
        Imports = $imports
        ResourceTypeIds = @(Get-PeResourceTypeIds -Path $Path)
    }
}

$guiPe = Get-PeInspection -Path $stagedExe
$consolePe = Get-PeInspection -Path $stagedConsoleExe
if ($guiPe.Subsystem -ne 2 -or $guiPe.SubsystemName -ne 'Windows GUI') {
    throw "Full Emerald.exe is not Windows GUI subsystem: $($guiPe.SubsystemName)."
}
if ($consolePe.Subsystem -ne 3 -or
    $consolePe.SubsystemName -ne 'Windows CUI') {
    throw ("Full Emerald Console.exe is not Windows CUI subsystem: " +
        $consolePe.SubsystemName)
}
if ($guiPe.EntryPoint -ne $consolePe.EntryPoint) {
    throw ("GUI/console entry points differ: $($guiPe.EntryPoint) vs " +
        $consolePe.EntryPoint)
}
if (@(Compare-Object $guiPe.Imports $consolePe.Imports).Count -ne 0) {
    throw 'GUI/console DLL import sets differ.'
}
foreach ($pe in @($guiPe, $consolePe)) {
    if ($pe.ResourceTypeIds -notcontains 3 -or
        $pe.ResourceTypeIds -notcontains 14) {
        throw ("Windows icon resources are missing from {0}. Found PE resource " +
            "type IDs: [{1}]." -f (Split-Path -Leaf $pe.Path),
            ($pe.ResourceTypeIds -join ', '))
    }
}

# Confirm that every imported non-system DLL is present beside both binaries.
$imports = @($guiPe.Imports)
$systemDlls = @(
    'comdlg32.dll', 'gdi32.dll', 'kernel32.dll', 'msvcrt.dll', 'ole32.dll',
    'opengl32.dll', 'shell32.dll', 'user32.dll', 'winmm.dll', 'ws2_32.dll'
)
foreach ($import in $imports) {
    if ($systemDlls -contains $import) { continue }
    if ($runtimeDlls.ToLowerInvariant() -notcontains $import) {
        throw "Unclassified non-system DLL import: $import"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $stagePath $import))) {
        throw "Imported DLL was not bundled: $import"
    }
}
foreach ($dll in $runtimeDlls) {
    if ($imports -notcontains $dll.ToLowerInvariant()) {
        throw "Expected runtime DLL is no longer imported: $dll"
    }
}

$manifestLines = @()
foreach ($relative in $expectedBeforeManifest | Sort-Object) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $stagePath $relative) `
        -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifestLines += "$hash  $($relative -replace '\\', '/')"
}
Set-Content -LiteralPath (Join-Path $stagePath 'SHA256SUMS.txt') `
    -Value $manifestLines -Encoding ASCII

$expectedFiles = @($expectedBeforeManifest + 'SHA256SUMS.txt')
Assert-NoForbiddenPayload -Directory $stagePath
Assert-ExactFileSet -Directory $stagePath -Expected $expectedFiles

Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    $stagePath,
    $zipPath,
    [System.IO.Compression.CompressionLevel]::Optimal,
    $false)

$zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $zipFiles = @(
        $zip.Entries |
            Where-Object { $_.Name } |
            ForEach-Object { $_.FullName -replace '/', '\' } |
            Sort-Object
    )
} finally {
    $zip.Dispose()
}
$missingZip = @($expectedFiles | Where-Object { $_ -notin $zipFiles })
$unexpectedZip = @($zipFiles | Where-Object { $_ -notin $expectedFiles })
if ($missingZip.Count -gt 0 -or $unexpectedZip.Count -gt 0) {
    throw ("ZIP allowlist mismatch. Missing: [{0}] Unexpected: [{1}]" -f
        ($missingZip -join ', '), ($unexpectedZip -join ', '))
}

# Extract the actual ZIP and prove that both entry modes load on a clean,
# PATH-independent folder. --help returns before the launcher/runtime can
# create personal data. Redirected output from the GUI-subsystem executable is
# also intentional: it proves mainCRTStartup still preserves CLI diagnostics
# when a caller explicitly supplies stdout/stderr handles.
[System.IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $auditPath)
Assert-NoForbiddenPayload -Directory $auditPath
Assert-ExactFileSet -Directory $auditPath -Expected $expectedFiles
$beforeSmoke = @(Get-RelativeFileList -Directory $auditPath)

function Invoke-PortableHelpSmoke {
    param(
        [Parameter(Mandatory = $true)][string]$ExecutableName,
        [Parameter(Mandatory = $true)][string]$Label
    )

    $stdoutPath = Join-Path $auditRoot "$stageName-$Label-help.stdout.txt"
    $stderrPath = Join-Path $auditRoot "$stageName-$Label-help.stderr.txt"
    foreach ($path in @($stdoutPath, $stderrPath)) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Force
        }
    }
    $process = Start-Process `
        -FilePath (Join-Path $auditPath $ExecutableName) `
        -ArgumentList '--help' `
        -WorkingDirectory $auditPath `
        -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath
    if ($process.ExitCode -ne 0) {
        throw ("Portable $Label --help smoke failed with exit code " +
            $process.ExitCode + '.')
    }
    if ((Get-Content -LiteralPath $stdoutPath -Raw) -notmatch 'Full Emerald') {
        throw "Portable $Label --help output did not identify Full Emerald."
    }
}

Invoke-PortableHelpSmoke -ExecutableName 'Full Emerald.exe' -Label 'gui'
Invoke-PortableHelpSmoke `
    -ExecutableName 'Full Emerald Console.exe' -Label 'console'

$afterSmoke = @(Get-RelativeFileList -Directory $auditPath)
if (@(Compare-Object $beforeSmoke $afterSmoke).Count -ne 0) {
    throw 'Portable --help smoke test unexpectedly mutated the extracted package.'
}

Remove-Item -LiteralPath $auditPath -Recurse -Force

$zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
Write-Host ''
Write-Host 'Portable release accepted.'
Write-Host "Folder : $stagePath"
Write-Host "ZIP    : $zipPath"
Write-Host "SHA256 : $zipHash"
Write-Host ("PE     : GUI=$($guiPe.EntryPoint)/$($guiPe.SubsystemName); " +
    "console=$($consolePe.EntryPoint)/$($consolePe.SubsystemName)")
Write-Host "Files  : $($expectedFiles.Count) (allowlist exact; no ROM/BIOS/save/state/source/log)"
