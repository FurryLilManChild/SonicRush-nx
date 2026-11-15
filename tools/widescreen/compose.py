#!/usr/bin/env python3
"""SonicRush-nx widescreen spike: rebuild DS screens from host video dumps.

usage: compose.py STEM [out.png]

STEM is a host NR_RAMDUMP prefix-<cycles> (files .bga .bgb .obja .objb .xpa
.xpb .pal .oam .regs). Rebuilds each engine's 256x192 picture from its BG1,
BG2, BG3 layers and sprites (no 3D layer, no blending, no windows) so the
decoding can be checked against the host screenshot (.ppm) before the same
code renders a wide view. Game data stays in the dumps; nothing is written
back.
"""
import struct
import sys

from PIL import Image


def rgb15(c):
    return ((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31)


class Dump:
    def __init__(self, stem):
        rd = lambda ext: open(stem + ext, 'rb').read()
        self.bg = [rd('.bga'), rd('.bgb')]
        self.obj = [rd('.obja'), rd('.objb')]
        xp = [rd('.xpa'), rd('.xpb')]
        self.bg_xpal = [x[:0x8000] for x in xp]
        self.obj_xpal = [x[0x8000:] for x in xp]
        pal = rd('.pal')
        # 0x000 main BG, 0x200 main OBJ, 0x400 sub BG, 0x600 sub OBJ
        self.bg_pal = [struct.unpack_from('<256H', pal, 0x000), struct.unpack_from('<256H', pal, 0x400)]
        self.obj_pal = [struct.unpack_from('<256H', pal, 0x200), struct.unpack_from('<256H', pal, 0x600)]
        self.oam = rd('.oam')
        r = rd('.regs')
        w = struct.unpack_from('<56I', r, 0)
        self.regs = [w[0:28], w[28:56]]
        self.scroll = []
        for e in range(2):
            o = 224 + e * 48
            self.scroll.append(dict(
                bgx=struct.unpack_from('<4H', r, o), bgy=struct.unpack_from('<4H', r, o + 8),
                refx=struct.unpack_from('<2i', r, o + 16), refy=struct.unpack_from('<2i', r, o + 24),
                pm=struct.unpack_from('<8h', r, o + 32)))

    def dispcnt(self, e):
        return self.regs[e][0]

    def bgcnt(self, e, i):
        w = self.regs[e][2 + i // 2]
        return (w >> (16 * (i & 1))) & 0xFFFF


def tile_px(chrmem, base, t, x, y, bpp8):
    if bpp8:
        o = base + t * 64 + y * 8 + x
        return chrmem[o] if o < len(chrmem) else 0
    o = base + t * 32 + y * 4 + x // 2
    if o >= len(chrmem):
        return 0
    b = chrmem[o]
    return (b >> 4) if x & 1 else (b & 15)


class Engine:
    """One 2D engine's layers, sampled at any (x, y) relative to its screen."""

    def __init__(self, d, e):
        self.d, self.e = d, e
        dc = d.dispcnt(e)
        self.dc = dc
        self.mode = dc & 7
        self.ext_pal = bool(dc & (1 << 30))
        self.char_off = ((dc >> 24) & 7) * 0x10000 if e == 0 else 0
        self.map_off = ((dc >> 27) & 7) * 0x10000 if e == 0 else 0

    def bg_enabled(self, i):
        if not (self.dc >> (8 + i)) & 1:
            return False
        if i == 0 and self.e == 0 and (self.dc >> 3) & 1:
            return False   # 3D layer, not drawn here
        return True

    def layer_kind(self, i):
        if i in (0, 1):
            return 'text'
        if self.mode == 5 and i in (2, 3):
            c = self.d.bgcnt(self.e, i)
            if not (c & 0x80):
                return 'ext16'
            return 'bitmap8' if not (c & 4) else 'bitmap16'
        return 'text'

    def sample_bg(self, i, sx, sy):
        """Colour (rgb) or None for layer i at screen pixel (sx, sy)."""
        d, e = self.d, self.e
        c = d.bgcnt(e, i)
        chrbase = self.char_off + ((c >> 2) & 15) * 0x4000
        mapbase = self.map_off + ((c >> 8) & 31) * 0x800
        mem = d.bg[e]
        kind = self.layer_kind(i)
        sc = d.scroll[e]
        if kind == 'text':
            size = (c >> 14) & 3
            w = 512 if size in (1, 3) else 256
            h = 512 if size in (2, 3) else 256
            x = (sx + sc['bgx'][i]) % w
            y = (sy + sc['bgy'][i]) % h
            tx, ty = x // 8, y // 8
            sb = 0
            if w == 512 and tx >= 32:
                sb += 1
            if h == 512 and ty >= 32:
                sb += 2 if w == 512 else 1
            o = mapbase + sb * 0x800 + ((ty % 32) * 32 + (tx % 32)) * 2
            ent = struct.unpack_from('<H', mem, o)[0]
            t, hf, vf, p = ent & 0x3FF, (ent >> 10) & 1, (ent >> 11) & 1, ent >> 12
            px, py = x % 8, y % 8
            if hf:
                px = 7 - px
            if vf:
                py = 7 - py
            bpp8 = bool(c & 0x80)
            v = tile_px(mem, chrbase, t, px, py, bpp8)
            if not v:
                return None
            if bpp8:
                if self.ext_pal:
                    slot = i if i >= 2 or not (c & 0x2000) else i + 2
                    col = struct.unpack_from('<H', d.bg_xpal[e], slot * 0x2000 + p * 512 + v * 2)[0]
                    return rgb15(col)
                return rgb15(d.bg_pal[e][v])
            return rgb15(d.bg_pal[e][p * 16 + v])
        if kind == 'ext16':
            size = (c >> 14) & 3
            n = 128 << size
            k = i - 2
            pa, pb, pcc, pd = sc['pm'][k * 4:k * 4 + 4]
            # affine: texture = ref + pa*x + pb*y (ref latched at frame start)
            fx = sc['refx'][k] + pa * sx + pb * sy
            fy = sc['refy'][k] + pcc * sx + pd * sy
            x, y = fx >> 8, fy >> 8
            if c & 0x2000:
                x %= n
                y %= n
            elif not (0 <= x < n and 0 <= y < n):
                return None
            tx, ty = x // 8, y // 8
            o = mapbase + (ty * (n // 8) + tx) * 2
            ent = struct.unpack_from('<H', mem, o)[0]
            t, hf, vf, p = ent & 0x3FF, (ent >> 10) & 1, (ent >> 11) & 1, ent >> 12
            px, py = x % 8, y % 8
            if hf:
                px = 7 - px
            if vf:
                py = 7 - py
            v = tile_px(mem, chrbase, t, px, py, True)
            if not v:
                return None
            if self.ext_pal:
                col = struct.unpack_from('<H', d.bg_xpal[e], i * 0x2000 + p * 512 + v * 2)[0]
                return rgb15(col)
            return rgb15(d.bg_pal[e][v])
        return None

    def bg_prio(self, i):
        return self.d.bgcnt(self.e, i) & 3

    def sprites(self):
        """Yield (prio, x, y, w, h, pixel_fn) for each OAM sprite of this engine."""
        d, e = self.d, self.e
        oam = d.oam[e * 0x400:(e + 1) * 0x400]
        sizes = {0: [(8, 8), (16, 16), (32, 32), (64, 64)],
                 1: [(16, 8), (32, 8), (32, 16), (64, 32)],
                 2: [(8, 16), (8, 32), (16, 32), (32, 64)]}
        dc = self.dc
        one_d = bool(dc & 0x10)
        bound = 32 << ((dc >> 20) & 3)
        out = []
        for n in range(128):
            a0, a1, a2 = struct.unpack_from('<3H', oam, n * 8)
            rs = (a0 >> 8) & 1
            if not rs and (a0 >> 9) & 1:
                continue
            mode = (a0 >> 10) & 3
            if mode == 3:
                continue   # bitmap OBJ: not used by the stage sprites checked so far
            shape = (a0 >> 14) & 3
            if shape == 3:
                continue
            w, h = sizes[shape][(a1 >> 14) & 3]
            x = a1 & 0x1FF
            if x >= 256:
                x -= 512
            y = a0 & 0xFF
            if y >= 192:
                y -= 256
            bpp8 = bool(a0 & 0x2000)
            tile = a2 & 0x3FF
            prio = (a2 >> 10) & 3
            pal = a2 >> 12
            hf = (not rs) and bool(a1 & 0x1000)
            vf = (not rs) and bool(a1 & 0x2000)
            out.append((prio, n, x, y, w, h, bpp8, tile, pal, hf, vf, one_d, bound, rs, a0, a1))
        return out

    def sprite_pixel(self, s, px, py):
        prio, n, x, y, w, h, bpp8, tile, pal, hf, vf, one_d, bound, rs, a0, a1 = s
        if hf:
            px = w - 1 - px
        if vf:
            py = h - 1 - py
        tx, ty = px // 8, py // 8
        if one_d:
            base = tile * bound
            per_row = (w // 8) * (64 if bpp8 else 32)
            o = base + ty * per_row + tx * (64 if bpp8 else 32)
        else:
            stride = 32
            o = (tile + ty * stride + tx * (2 if bpp8 else 1)) * 32
        mem = self.d.obj[self.e]
        if bpp8:
            idx = o + (py % 8) * 8 + (px % 8)
            v = mem[idx] if idx < len(mem) else 0
            if not v:
                return None
            return rgb15(self.d.obj_pal[self.e][v])
        idx = o + (py % 8) * 4 + (px % 8) // 2
        b = mem[idx] if idx < len(mem) else 0
        v = (b >> 4) if px & 1 else (b & 15)
        if not v:
            return None
        return rgb15(self.d.obj_pal[self.e][pal * 16 + v])

    def render(self, x0=0, y0=0, w=256, h=192, sprite_list=None):
        """Render the view [x0, x0+w) x [y0, y0+h) in this engine's screen coordinates."""
        backdrop = rgb15(self.d.bg_pal[self.e][0])
        img = Image.new('RGB', (w, h), backdrop)
        px = img.load()
        prio_buf = [[9] * w for _ in range(h)]
        layers = [i for i in (3, 2, 1, 0) if self.bg_enabled(i)]
        layers.sort(key=lambda i: (-self.bg_prio(i), -i))
        for i in layers:
            p = self.bg_prio(i)
            for yy in range(h):
                for xx in range(w):
                    col = self.sample_bg(i, x0 + xx, y0 + yy)
                    if col is not None:
                        px[xx, yy] = col
                        prio_buf[yy][xx] = p
        sprites = sprite_list if sprite_list is not None else self.sprites()
        if (self.dc >> 12) & 1:
            for s in sorted(sprites, key=lambda s: (-s[0], -s[1])):
                prio, n, sx, sy, sw, sh = s[:6]
                for py in range(sh):
                    yy = sy + py - y0
                    if not 0 <= yy < h:
                        continue
                    for pxx in range(sw):
                        xx = sx + pxx - x0
                        if not 0 <= xx < w or prio > prio_buf[yy][xx]:
                            continue
                        col = self.sprite_pixel(s, pxx, py)
                        if col is not None:
                            px[xx, yy] = col
        return img


def main():
    stem = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else stem + '-compose.png'
    d = Dump(stem)
    shot = Image.open(stem + '.ppm')
    pics = [Engine(d, e).render() for e in range(2)]
    # the host shot is top above bottom; main engine shows on top unless POWCNT swapped
    sheet = Image.new('RGB', (512, 384))
    sheet.paste(shot, (0, 0))
    sheet.paste(pics[0], (256, 0))
    sheet.paste(pics[1], (256, 192))
    sheet.save(out)
    print('wrote', out)


if __name__ == '__main__':
    main()
