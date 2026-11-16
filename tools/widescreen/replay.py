#!/usr/bin/env python3
"""Unpack a SonicRush-nx replay (captures/rpl-NNN.bin) into PNGs and sprite lists.

usage: replay.py rpl-001.bin outdir [first last]
Per frame: NNN-view.png (stage view), NNN-ds.png (both DS screens),
NNN-3d.png (our 3D layer, black where empty), NNN-spr.txt.
"""
import os, struct, sys
from PIL import Image

def main():
    src, out = sys.argv[1], sys.argv[2]
    lo = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    hi = int(sys.argv[4]) if len(sys.argv) > 4 else 10**9
    os.makedirs(out, exist_ok=True)
    f = open(src, 'rb')
    assert f.read(8) == b'SRNXRPL1'
    count, vw, vh = struct.unpack('<3i', f.read(12))
    hsz = 12 * 4
    for i in range(count):
        h = struct.unpack('<I6i5I', f.read(hsz))
        serial, w, hh, wx, wy, mx, my, nspr, nhud, dispcnt, v3d, _ = h
        ds = f.read(256 * 384 * 4)
        v3 = f.read(vw * vh * 4)
        view = f.read(vw * vh * 4)
        spr = f.read(8192)
        if not (lo <= i <= hi):
            continue
        Image.frombuffer('RGBA', (vw, vh), view, 'raw', 'BGRA', 0, 1).convert('RGB').save(f'{out}/{i:03d}-view.png')
        Image.frombuffer('RGBA', (256, 384), ds, 'raw', 'BGRA', 0, 1).convert('RGB').save(f'{out}/{i:03d}-ds.png')
        px = struct.unpack('<%dI' % (vw * vh), v3)
        e6 = lambda v: (v << 2) | (v >> 4)
        img = Image.new('RGB', (vw, vh))
        img.putdata([(e6(p & 63), e6((p >> 8) & 63), e6((p >> 16) & 63)) if (p >> 24) & 31 else (0, 0, 0) for p in px])
        img.save(f'{out}/{i:03d}-3d.png')
        with open(f'{out}/{i:03d}-spr.txt', 'w') as t:
            t.write(f'serial {serial} world {wx},{wy} main {mx},{my} dispcnt {dispcnt:08X} own3d {v3d}\n')
            for k in range(min(nspr + nhud, 384)):
                x, y, a01, a2 = struct.unpack_from('<iiII', spr, k * 16)
                t.write(f"{'world' if k < nspr else 'hud  '} {k:3d} x {x:4d} y {y:4d} a0 {a01 & 0xFFFF:04X} a1 {a01 >> 16:04X} a2 {a2 & 0xFFFF:04X}\n")
            aff = struct.unpack_from('<128h', spr, 8192 - 512)
            for g in range(32):
                v = aff[g * 4:g * 4 + 4]
                if v != (256, 0, 0, 256):
                    t.write(f'affine {g:2d} {v[0]} {v[1]} {v[2]} {v[3]}\n')
            bc, ba = struct.unpack_from('<II', spr, 8192 - 512 + 256)
            t.write(f'bldcnt {bc:04X} bldalpha {ba:04X}\n')
    print(f'{count} frames, {vw}x{vh}')

if __name__ == '__main__':
    main()
