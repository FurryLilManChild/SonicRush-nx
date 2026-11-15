"""Host check: does our stage view line up with the main engine's DS screen?

For each NR_RAMDUMP stem with a .wide (drawn with NR_WIDE_NOPASTE=1) and a
.wmain, compare our pixels with the DS screen in the main window and search
small offsets. Prints match fractions only."""
import glob, struct, sys
import numpy as np

def chans(a):
    return np.stack([(a >> 16) & 255, (a >> 8) & 255, a & 255], -1).astype(int)

for f in sorted(glob.glob(sys.argv[1] + '-*.wide')):
    b = open(f, 'rb').read()
    vw, vh, mx, my = struct.unpack_from('<4i', b, 0)
    A = chans(np.frombuffer(b[-vw * vh * 4:], dtype='<u4').reshape(vh, vw))
    M = chans(np.frombuffer(open(f[:-5] + '.wmain', 'rb').read(), dtype='<u4').reshape(192, 256))
    res = {}
    for dy in range(-3, 4):
        for dx in range(-3, 4):
            x0, y0 = mx + dx, my + dy
            xs0, ys0 = max(0, x0), max(0, y0)
            xs1, ys1 = min(vw, x0 + 256), min(vh, y0 + 192)
            if xs1 - xs0 < 64 or ys1 - ys0 < 48:
                continue
            a = A[ys0:ys1, xs0:xs1]
            c = M[ys0 - y0:ys1 - y0, xs0 - x0:xs1 - x0]
            res[(dx, dy)] = (a == c).all(-1).mean()
    if not res:
        print(f, 'main window mostly outside the view')
        continue
    best = max(res, key=res.get)
    print('%s main at (%d,%d) exact %.1f%%  best offset %s %.1f%%' % (f, mx, my, 100 * res.get((0, 0), 0), best, 100 * res[best]))
