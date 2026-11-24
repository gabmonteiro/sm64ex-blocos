#!/usr/bin/env python3
"""Importa um texture pack (formato de resource pack de jogos de blocos) para o mod Blocos.

Uso:
    python3 tools/importar_texturepack.py <pack.zip | pasta | versao.jar> [pasta_destino]
    python3 tools/importar_texturepack.py --remover [pasta_destino]

As texturas vao para a pasta de saves do jogo (padrao: ~/.local/share/sm64ex/gfx/textures/blocos),
que tem prioridade sobre as texturas originais do mod. Nada e copiado para o repositorio:
use apenas packs que voce tem o direito de usar.
"""
import io
import os
import shutil
import sys
import zipfile

from PIL import Image

DESTINO_PADRAO = os.path.expanduser("~/.local/share/sm64ex/gfx/textures/blocos")

# nome no mod -> caminhos possiveis no pack (formato novo 1.13+ e antigo)
MAPA = {
    "grama_topo": ["block/grass_block_top.png", "blocks/grass_top.png"],
    "terra": ["block/dirt.png", "blocks/dirt.png"],
    "pedra": ["block/stone.png", "blocks/stone.png"],
    "pedregulho": ["block/cobblestone.png", "blocks/cobblestone.png"],
    "tabuas": ["block/oak_planks.png", "blocks/planks_oak.png"],
    "tronco_lado": ["block/oak_log.png", "blocks/log_oak.png"],
    "tronco_topo": ["block/oak_log_top.png", "blocks/log_oak_top.png"],
    "tijolo": ["block/bricks.png", "blocks/brick.png"],
    "areia": ["block/sand.png", "blocks/sand.png"],
    "folhas": ["block/oak_leaves.png", "blocks/leaves_oak.png"],
    "espada": ["item/diamond_sword.png", "items/diamond_sword.png"],
    "arco": ["item/bow.png", "items/bow_standby.png"],
    "arco_1": ["item/bow_pulling_0.png", "items/bow_pulling_0.png"],
    "arco_2": ["item/bow_pulling_1.png", "items/bow_pulling_1.png"],
    "arco_3": ["item/bow_pulling_2.png", "items/bow_pulling_2.png"],
    "flecha": ["entity/projectiles/arrow.png", "entity/arrow.png"],
}
GRAMA_LADO = ["block/grass_block_side.png", "blocks/grass_side.png"]
GRAMA_LADO_OVERLAY = ["block/grass_block_side_overlay.png", "blocks/grass_side_overlay.png"]

COR_GRAMA = (145, 189, 89)    # tinta de bioma "planicie"
COR_FOLHAS = (119, 171, 47)


class Pack:
    def __init__(self, caminho):
        self.zip = None
        self.dir = None
        if os.path.isdir(caminho):
            self.dir = caminho
        else:
            self.zip = zipfile.ZipFile(caminho)
            self.nomes = {n.lower(): n for n in self.zip.namelist()}

    def abrir(self, rel):
        alvo = "assets/minecraft/textures/" + rel
        if self.zip:
            nome = self.nomes.get(alvo.lower())
            if nome is None:
                return None
            return Image.open(io.BytesIO(self.zip.read(nome))).convert("RGBA")
        p = os.path.join(self.dir, alvo)
        return Image.open(p).convert("RGBA") if os.path.exists(p) else None

    def primeiro(self, rels):
        for r in rels:
            im = self.abrir(r)
            if im is not None:
                return quadrado(im)
        return None


def quadrado(im):
    """Texturas animadas vem como tira vertical: usa so o primeiro quadro."""
    w, h = im.size
    return im.crop((0, 0, w, w)) if h > w else im


def tingir(im, cor):
    px = im.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            r, g, b, a = px[x, y]
            px[x, y] = (r * cor[0] // 255, g * cor[1] // 255, b * cor[2] // 255, a)
    return im


def opaco(im, fundo):
    """Os blocos do mod sao opacos: preenche pixels transparentes (folhas) com uma cor escura."""
    base = Image.new("RGBA", im.size, fundo + (255,))
    base.alpha_composite(im)
    return base


def flecha_ponta_a_direita(im):
    """A faixa lateral (u 0-16, v 0-5) deve ter a cauda a esquerda e a ponta (metal, mais escura) a direita."""
    esc = im.size[0] / 32.0
    faixa = im.crop((0, 0, int(16 * esc), int(5 * esc)))
    px = faixa.load()
    w, h = faixa.size

    def brilho(x0, x1):
        vals = [sum(px[x, y][:3]) for x in range(x0, x1) for y in range(h) if px[x, y][3] > 128]
        return sum(vals) / len(vals) if vals else 0

    quarto = max(1, w // 4)
    if brilho(0, quarto) < brilho(w - quarto, w):  # ponta escura esta a esquerda: espelha a faixa
        im.paste(faixa.transpose(Image.FLIP_LEFT_RIGHT), (0, 0))
    return im


def importar(origem, destino):
    pack = Pack(origem)
    os.makedirs(destino, exist_ok=True)
    feitos, faltando = [], []
    for nome, rels in MAPA.items():
        im = pack.primeiro(rels)
        if im is None:
            faltando.append(nome)
            continue
        if nome == "grama_topo":
            im = tingir(im, COR_GRAMA)
        elif nome == "folhas":
            im = opaco(tingir(im, COR_FOLHAS), (22, 48, 18))
        elif nome == "flecha":
            im = flecha_ponta_a_direita(im)
        im.save(os.path.join(destino, nome + ".rgba16.png"))
        feitos.append(nome)
    lado = pack.primeiro(GRAMA_LADO)
    if lado is not None:
        overlay = pack.primeiro(GRAMA_LADO_OVERLAY)
        if overlay is not None:
            overlay = tingir(overlay.resize(lado.size, Image.NEAREST), COR_GRAMA)
            lado.alpha_composite(overlay)
        lado.save(os.path.join(destino, "grama_lado.rgba16.png"))
        feitos.append("grama_lado")
    else:
        faltando.append("grama_lado")
    print("importadas %d texturas para %s" % (len(feitos), destino))
    if faltando:
        print("nao encontradas no pack (ficam as originais do mod):", ", ".join(faltando))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    if sys.argv[1] == "--remover":
        destino = sys.argv[2] if len(sys.argv) > 2 else DESTINO_PADRAO
        shutil.rmtree(destino, ignore_errors=True)
        print("texture pack removido; voltam as texturas originais do mod")
        return
    importar(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else DESTINO_PADRAO)


if __name__ == "__main__":
    main()
