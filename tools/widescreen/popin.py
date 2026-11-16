"""Host check for object pop-in: from NR_SPRLOG output, list world sprites
(drawn by the object draw path) that appear for the first time inside the DS
screen area +40 px, i.e. visibly inside the view rather than at its edge."""
import re, sys, collections
frames = []; cur = None
for line in open(sys.argv[1]):
    if line.startswith('[sprlog] frame'):
        m = re.search(r'cam (-?\d+),(-?\d+)', line); cur = {'cam': (int(m.group(1)), int(m.group(2))), 's': []}; frames.append(cur)
    elif line.startswith('[sprlog]  x') and cur:
        m = re.search(r'x\s+(-?\d+) y\s+(-?\d+) hud (\d) lr ([0-9A-F]+)', line)
        if m.group(4) == '0206BC38': cur['s'].append((int(m.group(1)), int(m.group(2))))
near = lambda a, b: abs(a[0] - b[0]) <= 3 and abs(a[1] - b[1]) <= 3
pops = []; prev = []
for i, f in enumerate(frames):
    cx, cy = f['cam']; W = [(x + cx, y + cy) for x, y in f['s']]
    if i > 3:
        for w in W:
            if not any(near(w, p) for p in prev):
                sx, sy = w[0] - cx, w[1] - cy
                if 200 < sx < 360 and -40 < sy < 232: pops.append((i, sx, sy))
    prev = W
print(len(frames), 'frames; first appearances near the right DS edge (x 200..360):', len(pops),
      'x histogram', sorted(collections.Counter((p[1] // 20) * 20 for p in pops).items()))
