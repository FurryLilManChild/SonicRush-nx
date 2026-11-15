# SonicRush-nx design

## Goal

A native static recompilation of Sonic Rush (USA, ASCE) for Switch homebrew: game functions are Switch functions with real calls and returns, game memory is plain Switch memory, timing is coarse and event-driven, only hardware registers go through a hardware layer. Presentation: one modern widescreen picture instead of two stacked DS screens.

## What is known about the game

- ARM9: 551,064 bytes uncompressed at 0x02000000, entry 0x02000800. Main program about 2,190 functions; stage loading (zone map and event archives) is referenced from the main program.
- Two ARM9 overlays sharing 0x022C5C80: ov00 (about 2,470 functions) and ov01 (about 1,620). The display capture register is referenced only in ov01 (3 literal uses).
- ARM7: 159,524 bytes at 0x02380000, about 900 functions.
- Stage data per act: `narc/zXY_map.narc` (LZ77-compressed tile, block and layout data), `narc/zXY_eve.narc` (object placement), `narc/zXY_act_lz7.narc` (sprites). Bosses: `mod/bs*_*.nsbmd` models.
- Screens: acts use both screens as one 256x384 playfield (character moves between them). Bosses run on one screen, the other shows the life gauge and a cheerleader. Special stages: 3D halfpipe on the top screen, stylus steering and touch-the-numbers prompts on the bottom. Menus are on the touch screen but fully usable with D-pad and A/B.

## Pieces

| Piece | Plan | Borrowed from |
|---|---|---|
| ARM9 and ARM7 code | Native C from `tools/nrecomp` (one C function per guest function) | MPH-nx |
| Hardware layer (2D, 3D, SPU, DMA, timers, IPC, card) | ndsrecomp's, patched as in MPH-nx; still emulation-style at first, replaced piece by piece | ndsrecomp, MPH-nx patch |
| BIOS | FreeBIOS (never a Nintendo BIOS) | ndsrecomp |
| 3D renderer | deko3d with resolution scaler | MPH-nx |
| Presenter and layouts | Phase 1: seamless column for acts; full-screen plus blended panels for bosses and special stages | MPH-nx presenter, PanelBlend |
| Menus | D-pad/stick and A/B through the game's own menu controls | game |
| Special stage | Stick steering through synthetic touch; touch prompts picked with the stick | MPH-nx weapon wheel pattern |
| Widescreen (phase 2) | Wider stage camera: level layers drawn from the full layout in memory, object activation and culling widened, HUD pinned to edges | new |

## Phases

1. Boot: native program to the title screen in the host test (cloud), then a first Switch build with the seamless column layout.
2. Playable: controls, menus, special stages, bosses; speed pass at stock clocks if needed.
3. Widescreen spike: answer tile streaming, object activation and how the 3D character shows on both screens.
4. Widescreen rework.
5. Release.

## Decisions

- 2026-11-14: Widescreen is the goal of the port; speed work waits until widescreen is proven.
- 2026-11-14: Widescreen is a display option for the player. Default view: the full height of both DS screens plus the 80 px gap, as one picture with the gap filled by the level (464 px tall, 825 px wide at 16:9). Mark expects to tweak it after seeing it (other sizes, such as 683 x 384, stay possible as options).
- 2026-11-14: Stacked (column) layout keeps an 80 px strip between the screens; stock clock (1020 MHz) is the target.

## Open decisions for Mark

- Version numbering and default clocks at release.
- Widescreen target width if 683 proves troublesome (fallback 512x384 with side art).
