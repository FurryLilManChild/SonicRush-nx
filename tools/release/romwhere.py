import sys, os, glob, subprocess, bisect
inputs, target = sys.argv[1], sys.argv[2]
data = open(target,'rb').read()
# section map: file offset -> vaddr
out = subprocess.run(['readelf','-SW',target],capture_output=True,text=True).stdout
secs=[]
for l in out.splitlines():
    p=l.replace('[ ','[').split()
    if len(p)>6 and p[0].startswith('['):
        try: secs.append((p[1],int(p[3],16),int(p[4],16),int(p[5],16)))
        except: pass
syms=[]
for l in subprocess.run(['nm','-C','-S','--defined-only',target],capture_output=True,text=True).stdout.splitlines():
    p=l.split(None,3)
    if len(p)==4: syms.append((int(p[0],16),int(p[1],16),p[3]))
syms.sort(); addrs=[s[0] for s in syms]
files=[os.path.join(inputs,'arm9.bin'),os.path.join(inputs,'arm7.bin')]+sorted(glob.glob(os.path.join(inputs,'overlays','*')))
for f in files:
    if os.path.isdir(f): continue
    b=open(f,'rb').read()
    for off in range(0,len(b)-32,1024):
        c=b[off:off+32]
        if len(set(c))<12: continue
        i=data.find(c)
        if i<0: continue
        va=None; sn='?'
        for n,a,o,sz in secs:
            if o<=i<o+sz: va=a+i-o; sn=n
        sym='?'
        if va is not None:
            k=bisect.bisect_right(addrs,va)-1
            if k>=0: sym=syms[k][2]
        print(os.path.basename(f),hex(off),sn,sym, c[:16].hex())
