#!/usr/bin/env python3
"""Gera as texturas procedurais (originais) dos blocos do mod "Blocos".

Cada textura e 16x16 RGBA, desenhada por codigo com paleta propria.
Uso: python3 tools/blocos_texturas.py <pasta_saida>
"""
import os
import random
import sys

from PIL import Image

N = 16


def clamp(v):
    return max(0, min(255, int(v)))


def noise_tex(seed, base, var):
    rnd = random.Random(seed)
    img = Image.new("RGBA", (N, N))
    px = img.load()
    for y in range(N):
        for x in range(N):
            d = rnd.uniform(-var, var)
            px[x, y] = (clamp(base[0] + d), clamp(base[1] + d), clamp(base[2] + d * 0.8), 255)
    return img


def grama_topo():
    img = noise_tex(1, (92, 160, 64), 26)
    px = img.load()
    rnd = random.Random(11)
    for _ in range(22):
        x, y = rnd.randrange(N), rnd.randrange(N)
        px[x, y] = (126, 190, 84, 255)
    return img


def terra():
    img = noise_tex(2, (128, 88, 56), 22)
    px = img.load()
    rnd = random.Random(12)
    for _ in range(14):
        x, y = rnd.randrange(N), rnd.randrange(N)
        px[x, y] = (96, 64, 40, 255)
    for _ in range(6):
        x, y = rnd.randrange(N), rnd.randrange(N)
        px[x, y] = (150, 140, 128, 255)
    return img


def grama_lado():
    img = terra()
    top = grama_topo().load()
    px = img.load()
    rnd = random.Random(13)
    for x in range(N):
        depth = 3 + rnd.randrange(3)
        for y in range(depth):
            px[x, y] = top[x, y]
    return img


def pedra():
    img = noise_tex(3, (132, 134, 140), 16)
    px = img.load()
    rnd = random.Random(14)
    for _ in range(5):
        x, y = rnd.randrange(N), rnd.randrange(N)
        for _ in range(rnd.randrange(3, 7)):
            px[x % N, y % N] = (98, 100, 106, 255)
            x += rnd.choice((-1, 0, 1))
            y += rnd.choice((0, 1))
    return img


def pedregulho():
    rnd = random.Random(4)
    img = Image.new("RGBA", (N, N))
    px = img.load()
    # pedras arredondadas claras com rejunte escuro (Voronoi com repeticao)
    pts = []
    for gy in range(3):
        for gx in range(3):
            pts.append(((gx + 0.5 + rnd.uniform(-0.3, 0.3)) * N / 3,
                        (gy + 0.5 + rnd.uniform(-0.3, 0.3)) * N / 3,
                        rnd.uniform(-22, 22)))
    for y in range(N):
        for x in range(N):
            ds = []
            for (cx, cy, sh) in pts:
                dx = min(abs(x - cx), N - abs(x - cx))
                dy = min(abs(y - cy), N - abs(y - cy))
                ds.append(((dx * dx + dy * dy) ** 0.5, sh))
            ds.sort()
            edge = ds[1][0] - ds[0][0]
            if edge < 0.9:
                g = 82 + rnd.uniform(-6, 6)
            else:
                # leve sombreado: centro da pedra mais claro
                g = 150 + ds[0][1] - ds[0][0] * 3 + rnd.uniform(-7, 7)
            px[x, y] = (clamp(g), clamp(g), clamp(g + 3), 255)
    return img


def tabuas():
    rnd = random.Random(5)
    img = Image.new("RGBA", (N, N))
    px = img.load()
    for y in range(N):
        row = y // 4
        shade = [0, -14, 8, -6][row]
        for x in range(N):
            if y % 4 == 3:
                c = (104, 70, 40)
            else:
                v = shade + rnd.uniform(-10, 10) + (6 if (x + row * 5) % 7 == 0 else 0)
                c = (182 + v, 136 + v, 84 + v * 0.7)
            px[x, y] = (clamp(c[0]), clamp(c[1]), clamp(c[2]), 255)
        # pregos/juntas alternadas
        if y % 4 == 1:
            jx = (row * 7 + 3) % N
            px[jx, y] = (90, 60, 34, 255)
    return img


def tronco_lado():
    rnd = random.Random(6)
    img = Image.new("RGBA", (N, N))
    px = img.load()
    cols = [rnd.uniform(-20, 20) for _ in range(N)]
    for y in range(N):
        for x in range(N):
            v = cols[x] + rnd.uniform(-8, 8)
            if x % 4 == 0:
                v -= 22
            px[x, y] = (clamp(112 + v), clamp(80 + v), clamp(50 + v * 0.6), 255)
    return img


def tronco_topo():
    rnd = random.Random(7)
    img = Image.new("RGBA", (N, N))
    px = img.load()
    for y in range(N):
        for x in range(N):
            dx, dy = x - 7.5, y - 7.5
            r = (dx * dx + dy * dy) ** 0.5
            if max(abs(dx), abs(dy)) > 6.6:
                v = rnd.uniform(-10, 10)
                c = (106 + v, 76 + v, 48 + v)
            else:
                ring = int(r) % 2
                v = (14 if ring else -6) + rnd.uniform(-6, 6)
                c = (196 + v, 158 + v, 102 + v)
            px[x, y] = (clamp(c[0]), clamp(c[1]), clamp(c[2]), 255)
    return img


def tijolo():
    rnd = random.Random(8)
    img = Image.new("RGBA", (N, N))
    px = img.load()
    for y in range(N):
        for x in range(N):
            row = y // 4
            off = 4 if row % 2 else 0
            if y % 4 == 3 or (x + off) % 8 == 7:
                v = rnd.uniform(-8, 8)
                c = (178 + v, 170 + v, 158 + v)
            else:
                v = rnd.uniform(-16, 16) + ((x + off) // 8 % 2) * 8
                c = (150 + v, 70 + v * 0.6, 54 + v * 0.5)
            px[x, y] = (clamp(c[0]), clamp(c[1]), clamp(c[2]), 255)
    return img


def areia():
    img = noise_tex(9, (220, 204, 150), 14)
    px = img.load()
    rnd = random.Random(19)
    for _ in range(10):
        x, y = rnd.randrange(N), rnd.randrange(N)
        px[x, y] = (196, 176, 122, 255)
    return img


def folhas():
    rnd = random.Random(10)
    img = Image.new("RGBA", (N, N))
    px = img.load()
    for y in range(N):
        for x in range(N):
            r = rnd.random()
            if r < 0.18:
                c = (30, 66, 26)
            elif r < 0.55:
                c = (58, 118, 44)
            else:
                c = (78, 146, 58)
            v = rnd.uniform(-8, 8)
            px[x, y] = (clamp(c[0] + v), clamp(c[1] + v), clamp(c[2] + v), 255)
    return img


def icone_espada():
    """Espada de cristal original: lamina azul-gelo na diagonal, guarda azul-aco, pomo dourado."""
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    px = img.load()
    lam, nuc, fio = (120, 232, 236, 255), (56, 170, 186, 255), (220, 252, 252, 255)
    contorno = (20, 40, 52, 255)
    # lamina: de (5,10) ate (14,1)
    for i in range(10):
        x, y = 5 + i, 10 - i
        for (dx, dy, c) in ((0, 0, nuc), (1, 0, lam), (0, -1, lam), (1, -1, fio)):
            if 0 <= x + dx < N and 0 <= y + dy < N:
                px[x + dx, y + dy] = c
    px[14, 0] = fio
    px[15, 0] = contorno
    # guarda (perpendicular a lamina)
    guarda = (76, 104, 140, 255)
    for (x, y) in ((2, 9), (3, 10), (4, 11), (5, 12), (6, 13), (3, 9), (6, 12)):
        px[x, y] = guarda
    px[2, 8] = (214, 168, 52, 255)
    px[7, 14] = (214, 168, 52, 255)
    # cabo e pomo
    for (x, y) in ((3, 12), (2, 13)):
        px[x, y] = (92, 60, 36, 255)
    px[1, 14] = (214, 168, 52, 255)
    px[0, 15] = (160, 120, 30, 255)
    return img


def icone_arco():
    """Arco de madeira original: curva bem aberta, corda clara e empunhadura escura."""
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    px = img.load()
    madeira, clara, escura, corda = (150, 104, 58, 255), (182, 134, 80, 255), (74, 46, 30, 255), (230, 230, 222, 255)
    A, B, C = (1.5, 13.5), (13.5, 1.5), (1.0, 1.0)
    pts = []
    for i in range(60):
        t = i / 59.0
        x = (1 - t) ** 2 * A[0] + 2 * (1 - t) * t * C[0] + t * t * B[0]
        y = (1 - t) ** 2 * A[1] + 2 * (1 - t) * t * C[1] + t * t * B[1]
        pts.append((int(round(x)), int(round(y)), t))
    for (x, y, t) in pts:
        if 0.42 < t < 0.58:
            c = escura
        else:
            c = madeira
        px[x, y] = c
        if x + 1 < N and y + 1 < N and px[x + 1, y + 1][3] == 0:
            px[x + 1, y + 1] = clara if c == madeira else escura
    for i in range(13):
        x, y = 2 + i, 13 - i
        if px[x, y][3] == 0:
            px[x, y] = corda
    return img

def _arco_base(img, puxada):
    """Desenha o arco (curva aberta para cima/esquerda) e a corda puxada para baixo/direita."""
    px = img.load()
    madeira, clara, escura, corda = (150, 104, 58, 255), (182, 134, 80, 255), (74, 46, 30, 255), (230, 230, 222, 255)
    A, B, C = (1.5, 13.5), (13.5, 1.5), (1.0 - puxada * 0.5, 1.0 - puxada * 0.5)
    for i in range(60):
        t = i / 59.0
        x = (1 - t) ** 2 * A[0] + 2 * (1 - t) * t * C[0] + t * t * B[0]
        y = (1 - t) ** 2 * A[1] + 2 * (1 - t) * t * C[1] + t * t * B[1]
        xi, yi = int(round(x)), int(round(y))
        c = escura if 0.42 < t < 0.58 else madeira
        px[xi, yi] = c
        if xi + 1 < N and yi + 1 < N and px[xi + 1, yi + 1][3] == 0:
            px[xi + 1, yi + 1] = clara if c == madeira else escura
    # corda: das pontas ate o ponto de encaixe puxado na diagonal (+x,+y)
    nock = (7.5 + puxada * 2.2, 7.5 + puxada * 2.2)
    for (a, b) in ((A, nock), (nock, B)):
        for i in range(24):
            t = i / 23.0
            x, y = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
            xi, yi = int(round(x)), int(round(y))
            if 0 <= xi < N and 0 <= yi < N and px[xi, yi][3] == 0:
                px[xi, yi] = corda
    return nock


def _arco_puxando(estagio):
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    puxada = (1.0, 1.6, 2.2)[estagio]
    nock = _arco_base(img, puxada)
    px = img.load()
    # flecha encaixada apontando para cima/esquerda
    haste, ponta, pena = (196, 160, 104, 255), (150, 156, 164, 255), (236, 236, 236, 255)
    nx, ny = int(round(nock[0])), int(round(nock[1]))
    for i in range(12):
        x, y = nx - i, ny - i
        if 0 <= x < N and 0 <= y < N:
            px[x, y] = ponta if i >= 9 else (pena if i < 2 else haste)
    if 0 <= nx - 11 < N and 0 <= ny - 10 < N:
        px[nx - 11, ny - 10] = ponta
    return img


def arco_1():
    return _arco_puxando(0)


def arco_2():
    return _arco_puxando(1)


def arco_3():
    return _arco_puxando(2)


def flecha():
    """Textura 32x32 da flecha (entidade). Faixa lateral em u 0-16, v 0-5: cauda a esquerda, ponta a direita."""
    img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    px = img.load()
    haste, ponta, ponta2, pena, pena2 = (196, 160, 104, 255), (150, 156, 164, 255), (110, 116, 124, 255), (236, 236, 236, 255), (200, 60, 50, 255)
    for x in range(2, 13):
        px[x, 2] = haste
    for (x, y) in ((0, 0), (1, 1), (0, 4), (1, 3), (2, 1), (2, 3), (3, 0), (3, 4), (4, 1), (4, 3)):
        px[x, y] = pena if (x + y) % 2 else pena2
    for (x, y) in ((12, 1), (12, 3), (13, 1), (13, 2), (13, 3), (14, 2), (15, 2), (12, 2)):
        px[x, y] = ponta if x < 14 else ponta2
    return img


TEXTURAS = {
    "grama_topo": grama_topo,
    "grama_lado": grama_lado,
    "terra": terra,
    "pedra": pedra,
    "pedregulho": pedregulho,
    "tabuas": tabuas,
    "tronco_lado": tronco_lado,
    "tronco_topo": tronco_topo,
    "tijolo": tijolo,
    "areia": areia,
    "folhas": folhas,
    "espada": icone_espada,
    "arco": icone_arco,
    "arco_1": arco_1,
    "arco_2": arco_2,
    "arco_3": arco_3,
    "flecha": flecha,
}


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "textures/blocos"
    os.makedirs(out, exist_ok=True)
    for nome, fn in TEXTURAS.items():
        fn().save(os.path.join(out, nome + ".rgba16.png"))
    print("geradas %d texturas em %s" % (len(TEXTURAS), out))


if __name__ == "__main__":
    main()
