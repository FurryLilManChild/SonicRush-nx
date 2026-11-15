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

## Open

- Who writes the camera (function), and whether those addresses hold in other acts (they are likely inside a heap-allocated stage object).
- Tile streaming into BG VRAM: how far past the view the game fills.
- Object activation range.
- How Sonic's 3D model is shown on both screens (display capture is referenced only in overlay 1).

## Consequence for the widescreen target

A single wide camera can show the full 464 px (192 + 80 + 192) at 16:9 (825 x 464) or crop to 384 tall (683 x 384). Showing the gap strip needs the game to draw it, which it never does on the DS, so either way the widescreen path draws the level ourselves (or makes the game draw a taller view).
