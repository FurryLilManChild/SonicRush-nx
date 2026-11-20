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
    magic = f.read(8)
    assert magic in (b'SRNXRPL1', b'SRNXRPL2')
    v2 = magic == b'SRNXRPL2'
    count, vw, vh = struct.unpack('<3i', f.read(12))
    hsz = 12 * 4
    for i in range(count):
        h = struct.unpack('<I6i5I', f.read(hsz))
        serial, w, hh, wx, wy, mx, my, nspr, nhud, dispcnt, v3d, pad = h
        ds_only = (pad >> 24) == 0xD5   # switch-test19: not a stage frame (both DS screens only)
        ds = f.read(256 * 384 * 4)
        v3 = f.read(vw * vh * 4)
        view = f.read(vw * vh * 4)
        spr = f.read(8192)
        if v2:
            f.read(256 * 1024)   # sprite memory as drawn
        if not (lo <= i <= hi):
            continue
        if ds_only:
            # cam0 x/y in wx/wy, cam1 x/y in mx/my; POWCNT1 bit 15 = engine A on the top screen, bit 3 = 3D on
            print(f'{i:03d} serial {serial} DS only: cam0 ({wx},{wy}) cam1 ({mx},{my}) DISPCNT {dispcnt:08X} '
                  f'POWCNT1 {pad & 0xFFFF:04X} (engine A on {"top" if pad & 0x8000 else "bottom"}, 3D {"on" if pad & 8 else "off"}) '
                  f'touch {"%d,%d" % (nspr & 255, (nspr >> 8) & 255) if nspr & 0x10000 else "up"}')
            Image.frombuffer('RGBA', (256, 384), ds, 'raw', 'BGRA', 0, 1).convert('RGB').save(f'{out}/{i:03d}-ds.png')
            # switch-test27: overlay functions that ran this frame
            n = struct.unpack_from('<I', spr, 0)[0]
            if 0 < n < 2047:
                fns = struct.unpack_from('<%dI' % n, spr, 4)
                with open(f'{out}/{i:03d}-fns.txt', 'w') as fh:
                    fh.write('\n'.join('ov%02d %08X' % (1 if a >> 31 else 0, a & 0x7FFFFFFF) for a in fns) + '\n')
            continue
        Image.frombuffer('RGBA', (vw, vh), view, 'raw', 'BGRA', 0, 1).convert('RGB').save(f'{out}/{i:03d}-view.png')
        Image.frombuffer('RGBA', (256, 384), ds, 'raw', 'BGRA', 0, 1).convert('RGB').save(f'{out}/{i:03d}-ds.png')
        px = struct.unpack('<%dI' % (vw * vh), v3)
        e6 = lambda v: (v << 2) | (v >> 4)
        img = Image.new('RGB', (vw, vh))
        img.putdata([(e6(p & 63), e6((p >> 8) & 63), e6((p >> 16) & 63)) if (p >> 24) & 31 else (0, 0, 0) for p in px])
        img.save(f'{out}/{i:03d}-3d.png')
        m = struct.unpack_from('<I', spr, 6656)[0]   # switch-test31: register writes by scanline
        if 0 < m < 128:
            with open(f'{out}/{i:03d}-lines.txt', 'w') as fh:
                for k in range(m):
                    a, v = struct.unpack_from('<II', spr, 6660 + 8 * k)
                    fh.write('line %3d %08X <- %08X (%d bytes)\n' % (a & 0xFFFF, 0x04000000 | ((a >> 16) & 0x1FFF), v, a >> 29))
        n = struct.unpack_from('<I', spr, 6144)[0]   # switch-test30: functions in stage frames too
        if 0 < n < 128:
            fns = struct.unpack_from('<%dI' % n, spr, 6148)
            with open(f'{out}/{i:03d}-fns.txt', 'w') as fh:
                fh.write('\n'.join('ov%02d %08X' % (1 if a >> 31 else 0, a & 0x7FFFFFFF) for a in fns) + '\n')
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
