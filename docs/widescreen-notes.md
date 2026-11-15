# Widescreen notes (phase 2 groundwork)

Found with the host test (scripted run into Leaf Storm Act 1, RAM dumps at 7300 to 8300 million ARM9 cycles, about 15 s of game time). Positions are 24.8 fixed point (value >> 8 = pixels); checked against Sonic's on-screen position in both snapshots.

## Stage camera (Act 1)

- Two per-screen view records at 0x0209004C and 0x02090054 (x, y each): same x, y always 0x11000 apart (272 px).
- The first record follows the screen Sonic is on and they swap order when he crosses; the other is the other screen.
- 272 = 192 (one screen) + 80: **the game keeps an 80-pixel hidden strip between the two screens**, like the gap on the real hardware. The seamless column (switch-test1) joins the screens edge to edge, so anything crossing between them jumps 80 px.
- Copies of the same values also at 0x0236AADC, 0x0236AAE4, 0x0236AB40, 0x0236AB48 (likely per-layer scroll for each screen).

## Sonic

- Position at 0x020907D0 (x) and 0x020907D4 (y), copies at 0x020907DC/E0 and 0x02090B54/58.
- Act start: x 828, y 754 px; camera x 700, so Sonic starts 128 px into the view.

## Camera records (global, overlay 0 data)

- Two records, 0x64 bytes each: 0x0236AAD8 (screen with Sonic) and 0x0236AB3C (other screen).
  - +0x04/+0x08 position (24.8), +0x0C/+0x10 previous position
  - +0x1E view width (s16, pixels): 256
  - +0x20 height scale (s16, 256 = 1.0); the object scanner uses height = scale * 0x1D0 (464 px = 192 + 80 + 192)
  - +0x48/+0x4C level minimum (8, 8 px), +0x50/+0x54 level maximum (Act 1: 5240 x 1848 px)
- The heap blocks found first (0x0209004C etc.) are a scroll manager's copies, refreshed each frame by task main 0x02335554 (ov00) from these records.

## Object activation (ov00)

- 0x0233545C (scroll manager state): when the camera moved, calls 0x02334EB4(obj, 0, 5).
- 0x02334EB4: for each of 3 placement layers, builds the rectangle camera.x - margin .. camera.x + width + margin, camera.y - margin .. camera.y + 464 + margin, walks it in 256 px cells and calls the layer's cell handler: 0x02334C18, 0x02334B34, 0x023348F0 (table 0x023685EC; margins via 0x023645C4 -> 0x0236AA78..80). Edge flags 0x10/0x20/0x40/0x80 mark cells clipped by the level bounds.
- Width comes from the camera record (+0x1E), so widening object activation may be a change to that field (to be tested: what else reads it, e.g. camera clamping at the level's right edge).

## Open

- Tile streaming into BG VRAM (zXY_map): a separate system from the object scanner; how far past the view it fills.
- Who else reads camera +0x1E (clamping, culling, 3D projection).
- How Sonic's 3D model is shown on both screens (display capture is referenced only in overlay 1).

## Consequence for the widescreen target

A single wide camera can show the full 464 px (192 + 80 + 192) at 16:9 (825 x 464) or crop to 384 tall (683 x 384). Showing the gap strip needs the game to draw it, which it never does on the DS, so either way the widescreen path draws the level ourselves (or makes the game draw a taller view).
