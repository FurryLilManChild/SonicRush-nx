#!/usr/bin/env python3
"""SonicRush-nx widescreen spike: one wide frame of a stage from host dumps.

usage: wide.py STEM [out.png] [width] [height]

Builds a single view (default 825 x 464: both DS screens plus the 80 px
gap, at 16:9) centred on the DS view, from:
  - BG1 far background: the top engine's layer, sampled past the DS screen
    (it wraps every 512 px and runs continuously across both screens),
  - BG2/BG3 level layers: drawn straight from the level layout in RAM
    (camera block + 0x130 block data, + 0x134 / + 0x138 layouts, 64 px blocks
    of 8x8 tile entries), so any width works,
  - sprites from both engines' OAM at their true screen offsets (top engine at
    y 0, bottom engine at y 272),
  - the 3D layer (Sonic) taken from the host screenshot where it differs from
    the rebuilt DS frame.
Sprites outside the two DS screens are not in OAM yet (the game clips them in
its sprite routine); the native sprite hook adds them.
"""
import struct
import sys

from PIL import Image

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from compose import Dump, Engine, rgb15  # noqa: E402

CAM = 0x0236AAD8


def main():
    stem = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else stem + '-wide.png'
    W = int(sys.argv[3]) if len(sys.argv) > 3 else 825
    H = int(sys.argv[4]) if len(sys.argv) > 4 else 464
    d = Dump(stem)
    ram = open(stem + '.bin', 'rb').read()
    R = lambda a, f='<I': struct.unpack_from(f, ram, a - 0x2000000)[0]

    cams = [(R(CAM + 4) >> 8, R(CAM + 8) >> 8), (R(CAM + 0x68) >> 8, R(CAM + 0x6C) >> 8)]
    # record 0 is the screen Sonic is on (main engine); the top screen is the one with smaller y
    top_eng = 0 if cams[0][1] <= cams[1][1] else 1
    bot_eng = 1 - top_eng
    tx, ty = cams[top_eng]
    left = (W - 256) // 2          # DS view's left edge inside the wide view
    wx0, wy0 = tx - left, ty       # world position of the wide view's top-left

    blk = R(CAM + 0x130)
    layouts = [R(CAM + 0x134), R(CAM + 0x138)]
    engs = [Engine(d, 0), Engine(d, 1)]
    te = engs[top_eng]

    def level_px(li, bgi, wx, wy):
        L = layouts[li]
        lw, lh = R(L, '<H'), R(L + 2, '<H')
        bx, by = wx // 64, wy // 64
        if not (0 <= bx < lw and 0 <= by < lh) or wx < 0 or wy < 0:
            return None
        b = R(L + 4 + 2 * (by * lw + bx), '<H')
        tx_, ty_ = (wx % 64) // 8, (wy % 64) // 8
        ent = R(blk + b * 128 + (ty_ * 8 + tx_) * 2, '<H')
        t, hf, vf, p = ent & 0x3FF, (ent >> 10) & 1, (ent >> 11) & 1, ent >> 12
        px, py = wx % 8, wy % 8
        if hf:
            px = 7 - px
        if vf:
            py = 7 - py
        c = d.bgcnt(te.e, bgi)
        chrbase = te.char_off + ((c >> 2) & 15) * 0x4000
        v = d.bg[te.e][chrbase + t * 64 + py * 8 + px]
        if not v:
            return None
        return rgb15(struct.unpack_from('<H', d.bg_xpal[te.e], bgi * 0x2000 + p * 512 + v * 2)[0])

    img = Image.new('RGB', (W, H), rgb15(d.bg_pal[te.e][0]))
    px = img.load()
    prio = [[9] * W for _ in range(H)]
    # layers back to front by DS priority (BG2 <- layout 0, BG3 <- layout 1)
    order = sorted([(te.bg_prio(1), 1), (te.bg_prio(2), 2), (te.bg_prio(3), 3)], key=lambda t: (-t[0], -t[1]))
    for p_, bgi in order:
        if not te.bg_enabled(bgi):
            continue
        for y in range(H):
            for x in range(W):
                if bgi == 1:
                    col = te.sample_bg(1, x - left, y)
                else:
                    col = level_px(bgi - 2, bgi, wx0 + x, wy0 + y)
                if col is not None:
                    px[x, y] = col
                    prio[y][x] = p_
    # sprites: with the native sprite records (.spr), the main engine's sprites at
    # their true positions cover the whole view; otherwise both engines' OAM
    import os
    sets = []
    if os.path.exists(stem + '.spr'):
        sys.path.insert(0, __file__.rsplit('/', 1)[0])
        from sprdump import read as read_spr
        recs = read_spr(stem + '.spr')[0]
        me = engs[0]
        lst = []
        for r in recs:
            t = me.sprite_from_attrs(r['a0'], r['a1'], r['a2'], r['slot'], int(r['x']), int(r['y']))
            if t is not None:
                lst.append(t)
        sets.append((0, 0, lst))
    else:
        sets = [(top_eng, 0, None), (bot_eng, 272, None)]
    for e, oy, lst in sets:
        eng = engs[e]
        if not (eng.dc >> 12) & 1:
            continue
        if lst is None:
            lst = eng.sprites()
        for s in sorted(lst, key=lambda s: (-s[0], -s[1])):
            sp, n, sx, sy, sw, sh = s[:6]
            for yy in range(sh):
                Y = oy + sy + yy
                if not 0 <= Y < H:
                    continue
                for xx in range(sw):
                    X = left + sx + xx
                    if not 0 <= X < W or sp > prio[Y][X]:
                        continue
                    col = eng.sprite_pixel(s, xx, yy)
                    if col is not None:
                        px[X, Y] = col
    # 3D layer: pixels of the real frame that the 2D rebuild does not explain
    shot = Image.open(stem + '.ppm').convert('RGB')
    sp_ = shot.load()
    main_on_top = top_eng == 0
    rebuilt = engs[0].render()
    rb = rebuilt.load()
    oy3 = 0 if main_on_top else 272
    sy0 = 0 if main_on_top else 192   # where the main engine sits in the host shot
    for y in range(192):
        for x in range(256):
            a = sp_[x, sy0 + y]
            b = rb[x, y]
            if max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2])) > 24:
                px[left + x, oy3 + y] = a
    img.save(out)
    print('wrote %s (%dx%d), top engine %d, camera %s' % (out, W, H, top_eng, cams))


if __name__ == '__main__':
    main()
