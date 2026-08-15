# Criando mods

O modelo de mods separa **pacote de dados** de **código confiável**.

## Pacote `.gbamod`

Um `.gbamod` é um ZIP contendo `manifest.toml` na raiz. Ele pode carregar dados
e opções, mas não DLLs, scripts executáveis ou bibliotecas nativas.

Exemplo mínimo:

```toml
format_version = 1
id = "example.emerald.display"
version = "1.0.0"
name = "Example Display Mod"
author = "Your name"
description = "Small presentation experiment."
license = "MIT"
resolver = "declarative"
save_compatibility = "shared"

[[target]]
game_id = "pokemon-emerald"
rom_sha1 = "18a2b0acdba046c71b8677bb58c9ee7d36f7a91f"

[[feature]]
id = "display-example"
name = "Display example"
description = "Enables the statically linked example plugin."
group = "Display"
default_enabled = false

[[plugin]]
feature = "display-example"
id = "example.emerald.display"
```

Use identificadores estáveis e semantic versioning. Um mod para outro hash deve
declarar outro `[[target]]` somente depois de ser validado naquela ROM.

## Plugin nativo

Comportamento C/C++ precisa fazer parte de uma build confiável do Full Emerald.
Ele registra um ID e callbacks através da API em
`gbarecomp/src/runtime/mod_runtime.h` e `native_mod_hooks.h`.

```cpp
static void activate_example() {
    gbarecomp::register_native_mod_frame_callback(on_frame);
    gbarecomp::register_native_mod_state_loaded_callback(on_state_loaded);
}

GBA_MOD_CONSTRUCTOR(register_example) {
    gba_mod_register_activation_plugin(
        "example.emerald.display", activate_example);
}
```

Callbacks devem ser curtos, determinísticos, `noexcept` na prática e respeitar
as fronteiras de frame/reset/state-load. Não mantenha ponteiros guest após reset
ou carregamento de state.

## Exemplo: Pokémon Follower

O mod incluído demonstra:

- gate por SHA-1;
- leitura segura de party, player e mapa;
- observadores e callbacks de frame;
- sprite guest com lifecycle explícito;
- tratamento de portas, warps e conexões abertas;
- invalidação após reset e save-state load;
- pacote importável e opção desativada por padrão.

O repositório **não inclui o pack de sprites**. Para gerar o arquivo local:

```powershell
.\tools\build_follower_sprite_pack.ps1 `
  -SourceRoot C:\sprites-autorizados `
  -OutputPath .\mods\preloaded\packages\pokemon-emerald.enhancement.follower\1.1.0\assets\follower_emerald.g3fs
```

O importador espera National Dex 001–386, quatro direções, dois frames e
variantes normal/shiny. Confira o script para a estrutura exata.

## Regras para contribuições de mods

1. Não inclua dados copiados da ROM, sprites oficiais, BIOS ou saves.
2. Declare a licença e a proveniência de cada asset.
3. Faça o recurso iniciar desligado.
4. Falhe fechado em hashes e layouts desconhecidos.
5. Garanta que desativar o mod preserve o caminho vanilla.
6. Teste reset, mapa, batalha, save state e carregamento repetido.
7. Documente impacto em save e compatibilidade com outros mods.

Mais detalhes do formato estão em
`gbarecomp/docs/MOD_PACKAGES.md`.
