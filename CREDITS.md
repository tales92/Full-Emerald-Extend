# Créditos e proveniência

Full Emerald só existe porque vários projetos tornaram públicas suas pesquisas e
ferramentas. Este documento distingue dependência, adaptação e inspiração.

## Fundação técnica

- **Matthew Stanley (`mstan`)** — autor de
  [EmeraldRecomp](https://github.com/mstan/EmeraldRecomp),
  [gbarecomp](https://github.com/mstan/gbarecomp) e colaborador principal de
  [recomp-ui](https://github.com/mstan/recomp-ui). A recompilação estática, o
  runtime GBA e a base do launcher vêm desse ecossistema R.A.I.D.
- **Contribuidores do recomp-ui e Dear ImGui** — backend e componentes da
  interface compartilhada. Consulte o histórico e as licenças do submódulo.
- **SDL contributors** — janela, áudio, input e compatibilidade de controles.

## Referências de engenharia

- **pret/pokeemerald contributors** —
  [pokeemerald](https://github.com/pret/pokeemerald) foi usado como referência
  semântica para nomes, estruturas e comportamento. Seu código C não é copiado
  para este repositório.
- **Bryant (`bryanthaboi`) e contribuidores** —
  [Gen1Recomp](https://github.com/bryanthaboi/gen1recomp) inspirou aspectos da
  experiência de controle e configuração. Full Emerald não usa o runtime
  Lua/LÖVE do projeto.

## Interface e mods

- **TwilitRealm / Dusklight** —
  [Dusklight](https://github.com/TwilitRealm/dusklight) inspirou a direção de
  navegação clean, menu lateral e foco controller-first. Nenhum código, ícone,
  vídeo ou asset do Dusklight foi copiado.
- **gamecorner-033 / PokePCFollowers** —
  [PokePCFollowers](https://github.com/gamecorner-033/PokePCFollowers) serviu de
  referência conceitual para seleção do seguidor. A implementação Full Emerald
  foi escrita nativamente para o runtime GBA e não copia o Lua ou os sprites do
  projeto.
- **DramaticShape** — o Dramatic Shape Voxel Mod motivou a pesquisa futura de
  renderização 3D. Nenhum código ou asset dele integra a versão 2.0.

## Full Emerald

- **Stone / ExtremestoneGG** — conceito do produto, direção, testes manuais,
  decisões de experiência e wordmark Full Emerald criado no Figma.
- **OpenAI Codex** — assistência de engenharia, auditoria, testes, documentação e
  implementação sob direção do mantenedor. A ferramenta de IA não substitui a
  autoria e revisão humana do projeto.

## Marcas e materiais do jogo

Pokémon, Pokémon Emerald e os personagens pertencem à Nintendo, Game Freak e
The Pokémon Company. Nenhuma dessas empresas patrocina ou endossa Full Emerald.

Capturas presentes na documentação têm finalidade limitada de demonstração e
identificação. Elas não são cobertas por nenhuma licença de código do projeto.
O ícone temporário de desktop com o personagem Grovyle também não faz parte de
uma licença de código e deverá ser substituído por uma marca totalmente original
antes de qualquer distribuição que exija direitos de branding independentes.

Se algum crédito ou proveniência estiver incorreto, abra uma issue com a fonte
original e a correção será priorizada.
