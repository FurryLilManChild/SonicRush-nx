# Progress log (newest first)

## 2026-11-19: switch-test31

- test30 result (Mark): title cards fine; zone 2 boss stacked whenever Sonic was on the ground; respawn showed the lines; zone 2 backgrounds (city cut off and repeating, sky underwater, water line).
- Boss stacking: cause, from rpl-003's function lists and Mark's description: the test29/30 card marker (0x023470A0) is not card-only; in the zone 2 boss it runs whenever Sonic is on the ground, and the card check came before the boss check. Replaced.
- One boss check for all bosses: overlay 0 0x023353C8 runs on every frame of the zone 1 boss (host, 1725 frames) and Mark's zone 2 boss (120), never in 8903 frames of stages, menus, cards, special stages. Zone 1 marker and the boss pixel check removed.
- Cards (act start, respawn, boss intro): in a stage (0x02346AA8 runs) without the play code (0x0234D5B8, 2866 of 2866 play frames, 0 of 1172 card frames). Host: act 1 card and boss cards switch exactly. Risk: the pause menu may count as a card.
- Far background: zone 2 BG1 is 512x512 but only the DS screen's part is kept current, hence the cut and repeat. Now the DS screen's BG1 rows are scaled evenly to the view width, bottom anchored, bilinear (fill_bg1_scaled); fast and reference drawing identical, 1.5-1.9 ms. Scroll speed is not matched (would need the stale columns): about 1.66x the DS on screen. Expected to also remove the sky underwater (no rows outside the DS screen any more); not reproduced on the host.
- Water line: no mid-frame 2D register writes or HBlank DMA in host underwater and above-water frames, and no full-screen 3D quad in one underwater frame. Cause not found. Stage replays now carry the last frame's 2D/DMA register writes with scanlines (spr offset 6656; replay.py NNN-lines.txt).
- Host: NR_WIDE=1 builds the stage view each frame again (the hook was lost); NR_RAMDUMP writes .wide.

## 2026-11-19: switch-test30

- test29 result (Mark): act title cards still wrong; special stage 2 has robots to tap (homing attack); zone 2 backgrounds glitch (sky where it should not be, bad stretching).
- Act title card: test29 identified the card (host) but the frontend only chose special layouts when no stage view was live, and the card shows while the stage view is live. My host check stopped at the identification and never reached the layout choice. Now a code-identified in-stage screen (14, and 15 for act clear once it has a signature) wins over the stage view.
- Emerald result: the layout swapped between result and special stage every frame or two for about 4 s at the end of special stage 2 (log 19:28, t=294-298 s); the result's code does not run every frame. Latched until the special stage code stops.
- Replays: stage frames now carry the overlay function list too (spr block offset 6144), so an L3 at the act clear screen gives its signature. replay.py writes NNN-fns.txt for them.
- Zone 2 background lead (not a cause): rpl-002 frame 60, the DS screens show sky and city through arches, the stage view draws a wall there. Not investigated yet.
- Act clear (rpl-003, "ACT1 CLEAR"): shown as the stage view in test29. No signature yet: a blind host playthrough of act 1 did not reach it.

## 2026-11-19: switch-test29

- Zone 2 freeze (test28, after the act title card): an interrupt taken twice in one IntrWait overwrote the wait's saved state with the handler's; the game resumed at 0x0200007C in IRQ mode and stopped. Fixed in swi_intr_wait (commit 3de3bd8). Host, Mark's save: Water Palace act 1 plays, timer runs.
- World map flicker (test28): the game waits for VBlank once a frame through the BIOS (0x02000078: svc 5). The BIOS halts inside its own loop, so any other interrupt returns into the loop. The port returned to the instruction after the svc, which ended the wait on any interrupt. Host trace: every 15th frame the wait ended with flags 0x808 (VBlank bit clear, timer/DMA interrupt from file loading), the next frame started at line 79-92 instead of 192 and BLDCNT was cleared mid-picture. Fix: an interrupt taken in the wait returns to NR_WAIT_RET with the mask in r1 and the resume PC in r12 (saved by the IRQ entry and by thread switches), and the wait continues. Host: 1035 of 1035 world map frames start at line 192 (before: 4 early in 53).
- Act title card: new code-identified screen 14 (overlay 0 0x023470A0 running and the stage update 0x0233D588 not yet seen), drawn as one tall picture of both screens like character select. Host: scene 14 from 3441M to 3754M, exactly the card. Only checked on zone 2 act 1 as Sonic.
- Roots: zone 2 misses from Mark's test28 log and the host run added (463 to 519 roots); host misses on the save route 62 to 24.
- Host diagnostics: NR_PCSAMPLE, NR_WAITTRACE, NR_CODELIST.

## 2026-11-18: switch-test19

- Stage view locked: the stacked-screen fallback (wide_mismatch) is gone. Only at an act start, while the stage view does not match either DS screen, the act title card is shown as the main engine's DS screen scaled to the view height and centred (option b). The window closes after 8 matching frames or 15 seconds.
- L3 replays everywhere. Outside the stage view the ring records both DS screens, both camera records, DISPCNT and POWCNT1 (marker 0xD5 in the head's last word); replay.py prints them. For boss and special stage planning.
- Late sprite memory copy (test18) off: it put sprite tiles one frame ahead (robot glitch in rpl-005).
- Rotation sanity check: a sprite matrix that scales past 4x or under 0.25x draws unrotated. Host: fast and reference sprite paths still agree (0 pixels differ). The affine source is still hardware OAM, not yet the table confirmed from a replay.

## 2026-11-15: switch-test3 result, our own camera (switch-test4)

- switch-test3 (Mark): wide view fine while the game has Sonic on the top screen; once it moves him to the bottom screen the DS screens appear misaligned over the wide view. Cause (from camera data): camera record 0 is the main engine's (3D layer) and follows Sonic; when he drops below the old screen line it moves 272 px below record 1 and the main engine goes to the bottom LCD. The view assumed record 0 = top screen. Also 52-55 fps fell to 18-21 fps with the CPU wide drawing on the game thread.
- Mark: write our own camera, and zoom in much closer, toward classic 2D Sonic framing (reference: Sonic Mania, 424x240).
- Sonic's position: [0x0236AC60] (camera target) +0x10 x, +0x14 y, 24.8; stable across a run spanning both screens.
- Our camera: look-ahead, vertical dead zone, capped follow speed, level clamp. Zoom: Close 424x240 (default, exactly 3x on 720p), Medium 512x288, Far 640x360, Full 825x464.
- Placement: the DS screen just drawn matches the camera latched two VBlanks back (VBlank hook in gpu3d.cpp); a sparse-grid search settles the last pixel. Host: offset 0 in all 19 checks across the screen switch. Colour conversion matches the DS 2D engine (5 to 6 to 8 bit).
- HUD sprites come from 0x02032C8C (lr 0x02032D14 into 0x02032D2C); fixed positions in all 10 checks; pinned to view corners and kept out of the DS picture.
- Drawing on the GPU (shaders/WideStage.glsl), CPU drawing as reference and fallback; the first 3 frames of each stage are compared GPU against CPU in the log.
- Known limit: large 3D scenery beyond the 256x192 area around Sonic is cut at that edge.
- switch-test4: NRO sha256 f215c5c1632248b87fcc618a812553115c7c43a47f7e030f6c4a90854a0f0efc; ELF /home/claude/build/elf-switch-test4.elf.

## 2026-11-15: widescreen sprites and the wide view in the Switch build (not built yet)

- Generator hooks: `tools/nrecomp/hooks/srnx.hooks` (NR_HOOKS) inserts our own C at guest addresses. Sprite clips found and hooked: central routine 0x020329F4 (per sprite), animated routine 0x02032E6C (whole sprite) and 0x02033058 (per part); true positions recorded after the slot allocator (0x02032B40, 0x020330D0). Allocator 0x02036BFC hands out a staging array at block +0x800 (main 0x0208A2D4, sub 0x02088DAC), sorted later into the OAM shadow at +0x000.
- A sprite kept only for the wide view is pushed to y 192 in OAM, so the DS screens stay exactly as before: host A/B, same state, 0 of 98,304 pixels differ with the hooks on.
- Act 1 host check: 49 main-engine sprites normally, 64 with the wide view (the extra ones left, right, in the strip and the bottom area); every record agrees with its OAM attributes.
- Camera width (+0x1E) is the wrong lever: the game recentres the camera on the wider view, which moves the 3D layer off the DS view and overruns the 512 px tile maps. The camera stays as the game sets it; the hooks widen around it.
- `switch/src/srnx_wide_render.cpp`: the wide view on the CPU (far background, both level layers from the layouts, recorded sprites), then the two DS screens pasted into their windows. Matches the Python reference outside the windows pixel for pixel (284,496 of 284,496).
- Presenter: a wide picture per swapchain slot, scaled to 1280x720. Port menu layout "Widescreen" (default): wide view in stages, Stacked elsewhere. Syntax-checked with the Switch compiler; no Switch build yet (waiting on Mark's OK).

## 2026-11-14: switch-test2 (3D busy-wait skip, stock clock, 80 px strip)

- switch-test1 (Mark, 1785 MHz because MPH-nx's boost default carried over): 50 to 57 fps dips in Act 1. Profile top: svcWaitProcessWideKeyAtomic 27% (game thread waiting). Per-frame counters: about 12,700 "3d" fences per frame in stages (GXSTAT reads, fence class 5).
- Cause found: SDK 3D reset at 0x0200431C spins on GXSTAT bit 27 (geometry busy); each read is a full sync on the Switch.
- Fix (SRNX, gpu3d.cpp): a busy GXSTAT read moves ARM9 time to when the wait can end (engine's remaining cycles, or the slice end while it waits for VBlank after SWAP_BUFFERS). Host A/B, scripted Act 1 run to 13.5e9 cycles: busy reads 110,593,831 -> 7,289,441; all 30 screenshots identical. Remaining polls are the VBlank wait (one per hardware event); follow-up idea: skip straight to VBlank.
- Stock clock default (cpu_boost 0); settings file renamed sonicrush-nx-settings.ini.
- Stacked layout keeps the 80 px strip: 397 x 298 per screen at x 441, bottom at y 422.
- 360 ARM9 and 18 ARM7 runtime roots.
- NRO 48 MB, sha256 e265a5c1eca1311310a9b2cbc6b61fa7089b011721d38f9ef5282e006f9706ad; ELF kept as /home/claude/build/elf-switch-test2.elf.

## 2026-11-14: switch-test1 (first Switch build)

- Layout: MPH-nx's Stacked layout (480x360 + 480x360, no gap) is the seamless column; default under SRNX. Boot splash skip off (Sonic Rush fades to white).
- Hunters-only modules replaced by inert versions: nx_lockon_none.cpp (lock-on reads Hunters memory), nx_menunav_none.cpp.
- Controls: --gamepad-bindings 12,13,14,15,2,1,4,3,10,11,7,0 (Switch A/B/X/Y = DS A/B/X/Y; SDL reports Switch buttons by Xbox position). Virtual stylus on the right stick, ZR holds, R3 taps (stopgap for special stages).
- Save: sonicrush.sav (EEPROM 8 KiB, detected by the runner).
- NRO 48 MB, sha256 92a20a075a1ccf06fed9e83bd8f36fcae21e31789d37e7f3e32bd3f1f0c07ca6. ELF kept as /home/claude/build/elf-switch-test1.elf. ROM-byte scan not run (test build only; required before release).

## 2026-11-14: host boot to title screen and attract demo (r1 to r8)

- r1: ARM9 parked at 0x0200089C; ARM7 stuck interpreting 0x03803D94 (Thumb `swi 3 ; bx lr`). Two causes: the BIOS call wrapper was never found, and an interpreter step that makes no progress never advanced time, so the ARM7 never yielded.
  Fixes: generator seeds every `swi n ; bx lr` stub (Thumb and ARM, both CPUs); both runtimes add 4 cycles when an interpreted step makes no progress, and log it.
- r3/r4: ARM7 IRQ handler (0x037FB458) sets LR by hand to its own tail (0x037FB4AC) before BX through the handler table. Analyzer now treats `LDR LR, [PC, #imm]` literals as code entries.
- r4: ARM7 dead at FreeBIOS reset vector (0x1078 `b .`). Cause: tier3 refuses to interpret code without write provenance, and native stores (crt0's WRAM copy) leave none. Fix (ndsrecomp patch): `g_nx_interp_any_pc`, set only around the native runtimes' single-step fallback.
- r5 (1.5e9 cycles, 4 s): title screen. r8 (6e9): attract demo (zone 1 boss) with 3D Sonic and the Tails panel. 236 ARM9 and 17 ARM7 runtime roots so far.
- missroots.py: `--arm7` mode, Thumb prologue walk-back.

## 2026-11-14: kickoff

- Feasibility: single-screen goal agreed (phase 1 seamless column, phase 2 true widescreen). ROM verified (ASCE rev 0, SHA-1 bc617530...).
- Repo set up from MPH-nx d3b4f5d. ndsrecomp on 179ac63 with MPH-nx's patch, applied cleanly.
- First generator pass: static 2,186 functions (100,621 instructions), ov00 2,465 (153,793), ov01 1,619 (96,678), ARM7 907 (36,359). 17 ARM9 units, 2 ARM7 units.
