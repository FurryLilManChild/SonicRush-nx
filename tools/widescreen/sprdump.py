"""Print widescreen sprite records from a host .spr dump (debug helper)."""
import struct, sys

def read(path):
    d = open(path, 'rb').read()
    off = 0
    out = []
    for e in range(2):
        n, fr = struct.unpack_from('<II', d, off); off += 8
        recs = []
        for i in range(n):
            x, y, slot, a0, a1, a2 = struct.unpack_from('<iiHHHH', d, off); off += 16
            recs.append(dict(x=x / 4096, y=y / 4096, slot=slot, a0=a0, a1=a1, a2=a2))
        out.append(recs)
    return out

if __name__ == '__main__':
    for e, recs in enumerate(read(sys.argv[1])):
        print('engine', e, len(recs))
        for r in recs:
            print('  slot %3d x %8.1f y %8.1f  a0 %04X a1 %04X a2 %04X' % (r['slot'], r['x'], r['y'], r['a0'], r['a1'], r['a2']))
