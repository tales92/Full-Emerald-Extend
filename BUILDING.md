# Compilando o Full Emerald

Este guia gera o código do jogo localmente a partir de uma ROM pertencente ao
usuário. ROM, BIOS e os arquivos C/C++ derivados nunca devem ser enviados ao
Git.

## Requisitos

- Windows 10 ou 11 x64;
- Git com suporte a submódulos;
- CMake 3.20+ e Ninja;
- compilador C/C++20 MinGW-w64 (MSYS2 recomendado);
- SDL2 para MinGW;
- dump próprio da BIOS GBA de 16 KiB;
- ROM compatível com SHA-1
  `18a2b0acdba046c71b8677bb58c9ee7d36f7a91f`.

## Checkout

```powershell
git clone --recursive https://github.com/ExtremestoneGG/Full-Emerald.git
cd Full-Emerald
```

Se o clone já existir:

```powershell
git submodule sync --recursive
git submodule update --init --recursive
```

## Gerar o motor da ROM

Coloque sua ROM fora do repositório ou dentro de
`variants/emerald_ptbr/roms/` (diretório ignorado pelo Git). Confira o hash:

```powershell
Get-FileHash -Algorithm SHA1 C:\caminho\para\emerald.gba
```

Compile o recompiler e traduza a ROM:

```powershell
cmake -S gbarecomp -B build-gbarecomp -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-gbarecomp --target gba_recompile --parallel 4

.\build-gbarecomp\gba_recompile.exe `
  --rom C:\caminho\para\emerald.gba `
  --config variants\emerald_ptbr\symbols\emerald_ptbr_zambrakas.toml `
  --out variants\emerald_ptbr\generated
```

Os shards em `variants/emerald_ptbr/generated/` são derivados da ROM e estão
ignorados por design.

## Mod Pokémon Follower (opcional)

O código do mod é compilável sem a arte. Para ativá-lo, gere o pacote a partir
de sprites direcionais que você tenha direito de usar:

```powershell
.\tools\build_follower_sprite_pack.ps1 `
  -SourceRoot C:\caminho\para\overworld `
  -OutputPath .\mods\preloaded\packages\pokemon-emerald.enhancement.follower\1.1.0\assets\follower_emerald.g3fs
```

O arquivo `.g3fs` é ignorado e não deve ser enviado ao repositório.

## Compilar o produto

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Gen3Recomp --parallel 4
```

O executável com interface gráfica é produzido como `Gen3Recomp-gui.exe`; o
empacotador o renomeia para `Full Emerald.exe`. O executável console permanece
disponível para testes automatizados e diagnóstico.

## Testes

```powershell
ctest --test-dir build --output-on-failure
ctest --test-dir build\recomp-ui-build --output-on-failure
```

Testes visuais e de save exigem inputs locais e nunca alteram o save original.
Os scripts em `scripts/` aceitam caminhos explícitos; os defaults usados pelo
desenvolvimento principal apontam para um volume de dados separado.

## Pacote portátil

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\make_release.ps1 `
  -Version 2.0.0 `
  -BuildDir .\build `
  -ReleaseDir D:\FullEmeraldRelease `
  -SkipBuild
```

O empacotador usa uma allowlist, verifica PE GUI/CUI, dependências, ícone e ZIP,
e rejeita ROM, BIOS, saves, states, logs e fontes geradas.

## Outras regiões

Não adicione um segundo hash ao mesmo motor. Cada revisão precisa de sua própria
geração, configuração, adapter e aceitação. A arquitetura prevista usa um
launcher comum que identifica o hash e despacha para o motor validado correto.
