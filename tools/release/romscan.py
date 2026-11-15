import sys, os, glob
inputs, target = sys.argv[1], sys.argv[2]
data = open(target,'rb').read()
files = [os.path.join(inputs,'arm9.bin'), os.path.join(inputs,'arm7.bin')] + sorted(glob.glob(os.path.join(inputs,'overlays','*')))
tot=hit=0
for f in files:
    if os.path.isdir(f): continue
    b = open(f,'rb').read()
    th=tt=0
    for off in range(0, len(b)-32, 1024):
        c = b[off:off+32]
        if len(set(c)) < 12: continue
        tt+=1
        if c in data: th+=1
    tot+=tt; hit+=th
    if th: print(f"  {os.path.basename(f)}: {th}/{tt}")
print(f"{os.path.basename(target)}: {hit}/{tot} sampled 32-byte chunks found")
