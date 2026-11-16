# Fraction of stage-view pixels that differ from the DS main screen where they overlap.
# usage: dsmatch.py stem...   (reads stem.wide and stem.wds)
import sys, struct
W, H = 424, 240
for stem in sys.argv[1:]:
    d = open(stem + '.wide', 'rb').read()
    h = struct.unpack('<42i', d[:168]); vw, vh, mx, my = h[0], h[1], h[2], h[3]
    px = struct.unpack('<%dI' % (vw * vh), d[-vw * vh * 4:])
    ds = struct.unpack('<%dI' % (2 * 256 * 192), open(stem + '.wds', 'rb').read())
    best = None
    for scr in (0, 1):
        n = diff = 0
        for y in range(0, 192, 4):
            vy = my + y
            if not 0 <= vy < vh: continue
            for x in range(0, 256, 4):
                vx = mx + x
                if not 0 <= vx < vw: continue
                a = px[vy * vw + vx] & 0xFFFFFF; b = ds[scr * 256 * 192 + y * 256 + x] & 0xFFFFFF
                n += 1
                if abs((a >> 16 & 255) - (b >> 16 & 255)) + abs((a >> 8 & 255) - (b >> 8 & 255)) + abs((a & 255) - (b & 255)) > 24: diff += 1
        if n: best = min(best or 1, diff / n)
    print(stem, 'main', mx, my, 'differs %.2f' % (best if best is not None else -1))
