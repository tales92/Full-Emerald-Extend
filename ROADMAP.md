# Roadmap

O roadmap descreve direção, não promessa de prazo.

## 2.0 — estabilização atual

- corrigir regressões do launcher e menu in-game;
- consolidar navegação completa por controle;
- estabilizar o Pokémon Follower em mapas, portas, rotas e estados;
- fortalecer recovery e isolamento do Multitelas;
- documentação, testes e distribuição reproduzível.

## Compatibilidade de ROM

- gerar motores separados para USA, França, Alemanha, Espanha e Japão;
- auditar símbolos, estruturas, RTC, Flash e adapters por hash;
- executar boot, gameplay, batalha, save/continue e oracle por revisão;
- adicionar um router único somente depois da aceitação individual.

## 2.1 — jogadores e conectividade

- perfis de jogador e saves separados;
- múltiplos controles com ownership explícito;
- layout local para dois jogadores;
- pesquisa e implementação de Link Cable;
- áudio e foco independentes por sessão.

## Mods

- storage por save para opções de mods;
- previews, descrição e restart-required no menu;
- SDK e exemplos menores;
- Pokémon Follower com lifecycle completo;
- pesquisa do overworld voxel/3D e, depois, batalhas 3D.

## Plataformas e renderização

- abstrair presenter para múltiplos backends;
- avaliar Vulkan sem colocá-lo como requisito da versão Windows;
- estudar Android depois que filesystem, input, áudio e lifecycle forem
  portáveis;
- preservar o caminho PPU fiel quando enhancements estiverem desligados.
