# Arquitetura

## Visão geral

```text
ROM do usuário
    │
    ├─ gba_recompile ──> shards C/C++ locais (não versionados)
    │
    └─ Full Emerald executable
          ├─ código ARM/Thumb recompilado
          ├─ gbarecomp runtime (CPU, PPU, APU, DMA, timers, Flash e RTC)
          ├─ recomp-ui (launcher + menu dentro do jogo)
          ├─ plugins nativos confiáveis
          └─ supervisor Multitelas (Windows, experimental)
```

O Full Emerald é uma recompilação estática, não uma recompilação do código-fonte
de `pret/pokeemerald`. Funções ARM/Thumb encontradas na ROM são traduzidas para
funções C/C++. O runtime implementa o restante do hardware observado pelo jogo.

## Fronteiras do projeto

### Repositório principal

Contém configuração por ROM, entrada do produto, integrações específicas do
Emerald, supervisor Multitelas, testes, scripts e plugins próprios.

### `gbarecomp`

Submódulo do motor genérico. Abriga CPU/runtime, hardware GBA, input, save state,
mod loader, observadores de funções e os callbacks de ciclo de vida.

### `recomp-ui`

Submódulo da interface compartilhada. O Full Emerald adiciona um tema e fluxo
próprios, mas mantém o backend SDL/OpenGL e os modelos do launcher reutilizáveis.

## Mod loader

Pacotes `.gbamod` são data-only. O loader valida caminhos, tamanho, CRC,
manifesto e alvo por `game_id + SHA-1`. Um pacote não fornece código executável.
Plugins nativos são registrados estaticamente e só podem ser ativados por um ID
conhecido no executável.

Callbacks nativos são multiplexados por um registry, evitando que um plugin
substitua silenciosamente outro observador. Frame, reset e state-load ocorrem em
fronteiras controladas pelo host.

## Pokémon Follower

O adapter de Emerald é fechado por SHA-1 e resolve símbolos/globais auditados.
O controller mantém um trail por tiles e o presenter cria um sprite do próprio
sistema guest. Portas e warps reinicializam o ciclo; conexões abertas entre
mapas preservam o mesmo sprite e fazem rebase das coordenadas locais sem piscar
ou teleportar.

O primeiro Pokémon saudável e não-Egg da party é selecionado. Bike, surf,
underwater e estados inseguros ocultam o seguidor conforme a política atual.

## Multitelas

O compositor é um supervisor Windows. Cada tela é um processo worker separado,
com save, states, configuração de mod e seed próprios. O supervisor envia input,
coordena comandos e lê framebuffers RGB por memória compartilhada.

O design evita o problema de dezenas de janelas independentes receberem
prioridades diferentes do desktop. Ainda assim, 12 CPUs GBA continuam sendo 12
simulações: a composição em uma janela reduz custo de apresentação, mas não
transforma trabalho de CPU em trabalho de GPU.

Workers lentos ou divergentes devem ser isolados sem derrubar sessões saudáveis.
Essa superfície continua experimental e deve ser tratada com cautela em hunts
longos.

## Saves e estados

- Save de bateria representa a Flash1M guest e é gravado atomicamente.
- Save states usam o formato GBAS do runtime e nove slots.
- Quick Save/Load agenda a operação numa fronteira segura depois que o dispatch
  gerado retornou.
- Multitelas mantém storage por worker e sincroniza explicitamente a tela
  escolhida ao voltar ao modo normal.

## Renderização

O caminho atual usa SDL2 e OpenGL para a interface/apresentação. A PPU GBA ainda
produz o framebuffer fiel. Vulkan não faz parte da versão 2.0; Android e um
backend 3D/voxel exigem uma abstração de presenter e compositor futura.
