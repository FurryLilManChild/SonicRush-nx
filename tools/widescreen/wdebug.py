"""Debug a host stage-view snapshot (.wide params + .wdata + .wmain): for
mismatching pixels in the main window, which of our layers (if any) shows the
DS screen's colour, and at what offset. Numbers only."""
import struct, sys, collections
import numpy as np
F = ['view_w','view_h','main_x','main_y','dispcnt','bg0','bg1','bg2','bg3','o0','o1','o2','o3','bg1_sx','bg1_sy',
     'world_x','world_y','lw0','lw1','lh0','lh1','nspr','nhud','hud_dx','hud_dy','bright','backdrop',
     'off_bg','off_obj','off_bgpal','off_objpal','off_xpal','off_lay0','off_lay1','off_blk','off_spr','off_main','main_valid']
stem = sys.argv[1]
b = open(stem + '.wide', 'rb').read()
P = dict(zip(F, struct.unpack_from('<4iI4I4I2i2i2I2I2I2iII10II', b, 0)))
D = open(stem + '.wdata', 'rb').read()
M = np.frombuffer(open(stem + '.wmain', 'rb').read(), dtype='<u4').reshape(192, 256) & 0xFFFFFF
u8 = lambda o: D[o]
u16 = lambda o: D[o] | (D[o + 1] << 8)
def ex(c5): c6 = c5 * 2; return (c6 << 2) | (c6 >> 4)
def rgb(c): return (ex(c & 31) << 16) | (ex((c >> 5) & 31) << 8) | ex((c >> 10) & 31)
dc = P['dispcnt']; BGV = 512 * 1024
def bg_pixel(bg, wx, wy, layout_off=None):
    c = P['bg%d' % bg]
    chrbase = ((dc >> 24) & 7) * 0x10000 + ((c >> 2) & 15) * 0x4000
    k = bg - 2
    if wx < 0 or wy < 0: return None
    bx, by = wx >> 6, wy >> 6
    lw, lh = P['lw%d' % k], P['lh%d' % k]
    if bx >= lw or by >= lh: return None
    lo = P['off_lay1'] if k else P['off_lay0']
    blk = u16(lo + 2 * (by * lw + bx))
    ent = u16(P['off_blk'] + blk * 128 + (((wy & 63) >> 3) * 8 + ((wx & 63) >> 3)) * 2)
    px, py = wx & 7, wy & 7
    if ent & 0x400: px = 7 - px
    if ent & 0x800: py = 7 - py
    v = u8(P['off_bg'] + ((chrbase + (ent & 0x3FF) * 64 + py * 8 + px) & (BGV - 1)))
    if not v: return None
    return rgb(u16(P['off_xpal'] + k * 8192 + 2 * ((ent >> 12) * 256 + v)))
mx, my = P['main_x'], P['main_y']; wx0, wy0 = P['world_x'], P['world_y']
print('params', {k: P[k] for k in ('view_w','view_h','main_x','main_y','world_x','world_y','lw0','lh0','lw1','lh1')},
      'bgcnt', [hex(P['bg%d' % i]) for i in range(4)], 'order', [P['o%d' % i] for i in range(4)], 'dispcnt', hex(dc))
# for band rows 96..127 of the DS screen: which layer/offset explains the DS colour
stats = collections.Counter()
for Y in range(96, 128, 4):
    for X in range(0, 256, 4):
        ds = int(M[Y, X]); wx, wy = wx0 + mx + X, wy0 + my + Y
        hit = []
        for bg in (2, 3):
            for dy in (0, -8, 8, -16, 16, -64, 64):
                for dx in (0, -8, 8, -64, 64):
                    c = bg_pixel(bg, wx + dx, wy + dy)
                    if c == ds: hit.append((bg, dx, dy))
        stats[tuple(hit[:2]) if hit else 'none'] += 1
for k, n in stats.most_common(12): print(n, k)
