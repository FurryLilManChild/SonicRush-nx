import struct,sys,zlib
d=open(sys.argv[1],'rb').read()
u32=lambda o:struct.unpack_from('<I',d,o)[0]
u16=lambda o:struct.unpack_from('<H',d,o)[0]
print('title',d[0:12],'code',d[12:16],'maker',d[16:18],'unit',d[0x12],'devcap',d[0x14],'ver',d[0x1E])
print('arm9 off %x entry %x ram %x size %x'%(u32(0x20),u32(0x24),u32(0x28),u32(0x2C)))
print('arm7 off %x entry %x ram %x size %x'%(u32(0x30),u32(0x34),u32(0x38),u32(0x3C)))
fnt,fnts,fat,fats=u32(0x40),u32(0x44),u32(0x48),u32(0x4C)
ov9,ov9s,ov7,ov7s=u32(0x50),u32(0x54),u32(0x58),u32(0x5C)
print('fnt %x/%x fat %x/%x ov9 %x/%x ov7 %x/%x'%(fnt,fnts,fat,fats,ov9,ov9s,ov7,ov7s))
print('used rom size %x, banner %x'%(u32(0x80),u32(0x68)))
print('logo crc %04x hdr crc %04x'%(u16(0x15C),u16(0x15E)))
# crc16 modbus-ish for header
def crc16(b):
    c=0xFFFF
    for x in b:
        c^=x
        for _ in range(8):
            c=(c>>1)^0xA001 if c&1 else c>>1
    return c
print('calc logo %04x hdr %04x'%(crc16(d[0xC0:0x15C]),crc16(d[0:0x15E])))
print('arm9 overlays',ov9s//32)
for i in range(ov9s//32):
    o=ov9+i*32
    oid,ram,sz,bss,si,se,fid,flags=struct.unpack_from('<8I',d,o)
    print(' ov%02d ram %08x size %6x bss %5x file %3d comp %s'%(oid,ram,sz,bss,fid,'Y' if flags>>24&1 else 'n'))
# file names
nfiles=fats//8
def walk(dirid,path):
    off=fnt+(dirid&0xFFF)*8
    sub,first,_=struct.unpack_from('<IHH',d,off)
    p=fnt+sub; fid=first; out=[]
    while True:
        l=d[p];p+=1
        if l==0:break
        name=d[p:p+(l&0x7F)].decode('latin1');p+=l&0x7F
        if l&0x80:
            did=u16(p);p+=2
            out+=walk(did,path+name+'/')
        else:
            s,e=struct.unpack_from('<II',d,fat+fid*8)
            out.append((fid,path+name,e-s));fid+=1
    return out
files=walk(0xF000,'')
print('files',len(files),'of fat',nfiles)
import collections
dirs=collections.Counter(f[1].rsplit('/',1)[0] if '/' in f[1] else '.' for f in files)
for k,v in sorted(dirs.items()):print(' dir',k,v)
open(sys.argv[2],'w').write('\n'.join('%4d %s %d'%f for f in files))
