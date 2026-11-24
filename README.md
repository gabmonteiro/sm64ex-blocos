# sm64ex-blocos

Um mod para o porte de PC do **Super Mario 64** ([sm64ex](https://github.com/sm64pc/sm64ex)) que coloca
mecânicas de jogos de construção com blocos dentro das fases originais. Dá para construir e quebrar blocos,
lutar com espada e arco e flecha, jogar em primeira pessoa e se mover com uma física no estilo
"sandbox de blocos", tudo rodando nas fases, com os inimigos e as estrelas do jogo original.

> **Este repositório não contém a ROM nem nenhum gráfico, som ou música do jogo.** Na compilação, os
> assets são extraídos da **sua própria cópia** da ROM (`baserom.us.z64`). Projeto de fãs, sem fins
> lucrativos, sem afiliação com a Nintendo ou a Mojang.

## O que o mod adiciona

- **Camada de blocos com colisão de verdade**: os blocos viram chão, parede e teto para o jogador e para os
  inimigos. Ficam salvos por fase e área em `blocos.dat`.
- **9 tipos de bloco**: grama, terra, pedra, pedregulho, tábuas, tronco, tijolo, areia e folhas. As texturas são
  geradas por código (`tools/blocos_texturas.py`), com paleta própria.
- **Espada**: golpe com animação de arco, alcance de ~3 blocos e acerto em até 3 inimigos à frente.
- **Arco e flecha**: segure para puxar e solte para atirar. A força depende do tempo puxando, e a flecha tem
  gravidade, arrasto no ar e na água, crava em blocos e no terreno e acerta inimigos.
- **Primeira pessoa** (`F5`) com o item na mão, a animação de golpe e o balanço ao andar.
- **Movimentação nova** (`F6` alterna com a do Mario original): caminhar, correr com `Ctrl`, agachar com
  `Shift` sem cair das beiradas, pulo de ~1,25 bloco e dano de queda acima de 3 blocos.
- **Mineiro**: personagem quadradão **original** (capacete amarelo com lanterna, camisa laranja, calça azul).
  Ele aproveita o esqueleto e as animações do jogo.
- **HUD**: mira, hotbar de 9 slots com ícones, nome do item e barra de carga do arco.
- **Suporte a texture packs**: importe um resource pack seu e os blocos, a espada, o arco (com os 3 estágios
  de puxar) e a flecha passam a usar as texturas dele. Os itens na mão são **sprites extrudados** pixel a
  pixel, então ficam iguais ao sprite do pack.

Portas, placas, diálogos, canhões, postes, estrelas e inimigos continuam funcionando como no jogo original.

## Controles

| Ação | Teclado / mouse |
|---|---|
| Andar | `W` `A` `S` `D` |
| Pular / avançar diálogo / pular a introdução / começar na tela de título | `Espaço` |
| Correr | `Ctrl` (segurando) |
| Agachar (não cai das beiradas) | `Shift` (segurando) |
| Ler placas / conversar | `E` (ou `,`) |
| Pausa | `Enter` ou `Esc` |
| Quebrar bloco / atacar com a espada | Botão esquerdo |
| Colocar bloco / puxar o arco (solte para atirar) | Botão direito |
| Copiar o bloco mirado para o slot | Botão do meio |
| Escolher slot | `1`–`9` ou roda do mouse |
| Trocar o tipo de bloco do slot | Aperte de novo o número do slot já selecionado |
| 1ª / 3ª pessoa | `F5` |
| Movimentação nova / movimentação do Mario | `F6` |
| Tela cheia | `F4` |

Hotbar padrão: **espada, arco, grama, terra, pedra, pedregulho, tábuas, tronco, tijolo**. Em 3ª pessoa, a
mira é o centro da tela.

## Texture packs

O mod vem com texturas próprias geradas por código. Para usar um texture pack no formato de resource pack
(`assets/minecraft/textures/...`, formato novo 1.13+ ou antigo):

```bash
python3 tools/importar_texturepack.py ~/Downloads/MeuPack.zip    # também aceita uma pasta ou um .jar
python3 tools/importar_texturepack.py --remover                   # volta para as texturas originais
```

As texturas vão para `~/.local/share/sm64ex/gfx/textures/blocos/`, que tem prioridade sobre as do mod, e
não precisa recompilar. A grama e as folhas recebem a tinta verde de bioma, e as folhas ficam opacas. O que
faltar no pack continua com a textura original do mod. **Nenhuma textura de pack é incluída neste
repositório**: use apenas packs que você tem o direito de usar.

| Mod | Arquivo no pack |
|---|---|
| grama (topo/lado), terra, pedra, pedregulho | `block/grass_block_top`, `grass_block_side` (+`_overlay`), `dirt`, `stone`, `cobblestone` |
| tábuas, tronco (lado/topo), tijolo, areia, folhas | `block/oak_planks`, `oak_log`, `oak_log_top`, `bricks`, `sand`, `oak_leaves` |
| espada, arco, arco puxando 1–3 | `item/diamond_sword`, `item/bow`, `item/bow_pulling_0..2` |
| flecha (entidade, planos cruzados) | `entity/projectiles/arrow` |

## Física de movimento

A física é uma implementação própria, feita a partir de valores públicos do estilo de jogo, e roda em
tiques de 20 Hz. A posição é integrada a cada frame de 30 fps, então o movimento fica suave e as velocidades
por segundo continuam corretas.

| Grandeza | Valor | Medido no jogo |
|---|---|---|
| Caminhar | ~4,3 blocos/s | 4,21 blocos/s |
| Correr | ×1,3 | 5,53 blocos/s |
| Agachado | ×0,3 | não cai da beirada |
| Pulo | ~1,25 bloco | 1,26 bloco |
| Gravidade | 0,08 bloco/tique², arrasto vertical 0,98 | |
| Atrito | chão 0,6×0,91 · ar 0,91 | |
| Flecha | até 3 blocos/tique, gravidade 0,05, arrasto 0,99 (água 0,6) | |

1 bloco = 100 unidades do jogo.

## Como compilar (Ubuntu 22.04+)

```bash
sudo apt install -y build-essential git python3 python3-pil libglew-dev libsdl2-dev libaudiofile-dev
git clone https://github.com/gabmonteiro/sm64ex-blocos.git
cd sm64ex-blocos
cp /caminho/da/sua/rom/"Super Mario 64 (USA).z64" baserom.us.z64   # ROM americana (SHA1 9bef1128...)
make VERSION=us RENDER_API=GL BETTERCAMERA=1 NODRAWINGDISTANCE=1 EXTERNAL_DATA=1 -j$(nproc)
cd build/us_pc && ./sm64.us.f3dex2e
```

O mod exige `EXTERNAL_DATA=1`, porque as texturas dos blocos e dos itens ficam no pacote `res/base.zip`. Para
regerar as texturas: `python3 tools/blocos_texturas.py textures/blocos`.

## Como funciona

Todo o mod está em `src/game/voxel*.c`, com ganchos pequenos no código do jogo:

| Arquivo | Responsabilidade |
|---|---|
| `src/game/voxel.c` | Mundo de blocos (tabela hash por fase/área), colisão, malha, raio de mira (DDA), câmera em 1ª pessoa, hotbar, HUD, salvar/carregar e testes automáticos |
| `src/game/voxel_items.c` | Espada e arco: modelos em código, animação de golpe, física das flechas, acerto em inimigos, item na mão em 1ª e 3ª pessoa |
| `src/game/voxel_move.c` | Movimentação nova: duas ações próprias (chão/ar) que substituem as ações de locomoção do Mario |
| `src/game/voxel_char.c` | Personagem Mineiro: troca as malhas do corpo por caixas na hora de renderizar e mantém o esqueleto e as animações |
| `tools/blocos_texturas.py` | Gera as texturas procedurais dos blocos, os ícones dos itens, os estágios do arco e a flecha |
| `tools/importar_texturepack.py` | Importa um resource pack para a pasta de saves (tinta de bioma, folhas opacas, orientação da flecha) |

Ganchos no jogo:

- `surface_collision.c`: `find_floor`, `find_ceil` e `find_wall_collisions` também consultam os blocos e
  devolvem superfícies alinhadas à grade, então tudo que usa colisão "enxerga" os blocos.
- `rendering_graph_node.c`: a malha de blocos e as flechas entram no nó da câmera, e as malhas do corpo são
  trocadas em `geo_append_display_list`.
- `camera.c`: a câmera de 1ª pessoa é aplicada no fim de `update_camera`.
- `mario.c`, `mario_actions_moving.c`, `mario_actions_airborne.c`: as ações de locomoção são convertidas para as
  novas, e o resto (água, portas, diálogos, dano, postes) continua no código original.
- `interaction.c`: portas e conversas aceitam a nova ação de chão.
- `level_update.c`, `hud.c`, `controller_sdl2.c`, `gfx_sdl2.c`, `configfile.c`: laço por frame, HUD, mouse
  e novos atalhos padrão.

Algumas decisões que podem ajudar quem for estudar o código:

- O porte ignora pedidos de limpar o z-buffer no meio do frame, então o item na mão em 1ª pessoa é montado
  no espaço da câmera, com as faces ordenadas de trás para a frente.
- O modelo do personagem é desenhado com escala 0,25, então as unidades locais dos ossos são 4× maiores que as
  do mundo.
- Não religue `G_ZBUFFER` no fim do HUD: o skybox do frame seguinte passa a gravar profundidade e esconde a
  cena 3D inteira.

## Testes automáticos

Com a variável `BLOCOS_TESTE`, o jogo pula o título e a seleção de arquivo, roda um cenário e imprime
medidas no terminal (`[blocos] ...`). Use sempre `--savepath` com uma pasta separada para não mexer nos seus
saves.

```bash
BLOCOS_TESTE=10 ./sm64.us.f3dex2e --skip-intro --savepath /tmp/teste_blocos
```

| Modo | Cenário |
|---|---|
| 1 | Constrói uma cena de teste (3ª pessoa) |
| 2 | 1ª pessoa: quebra um bloco e coloca um tijolo |
| 3 | Anda e pula, subindo uma escada de blocos |
| 4 | Estresse: cerca de 2.700 blocos com a câmera girando (mostra os FPS) |
| 5 | Não constrói nada (testa carregar o `blocos.dat`) |
| 6 / 8 / 9 | Câmera de retrato, de frente para o personagem (sem item / espada / arco) |
| 7 | Golpe de espada e tiro com o arco em 1ª pessoa |
| 10 | Mede caminhada, corrida e altura do pulo, e testa agachar na beirada |
| 11 | Introdução de save novo: aperta Espaço para pulá-la (rode sem `--skip-intro`) |

## Créditos

- Decompilação do Super Mario 64: equipe [n64decomp/sm64](https://github.com/n64decomp/sm64).
- Porte de PC: [sm64pc/sm64ex](https://github.com/sm64pc/sm64ex) (README original em `README_sm64ex.md`).
- Mod "Blocos": Gabriel Monteiro, com Claude Code.

Super Mario 64 é marca da Nintendo. Este projeto não distribui nenhum conteúdo do jogo, e você precisa ter
a sua própria cópia.
