# Progress log (newest first)

## 2026-11-21: switch-test36

- test35 result (Mark): level select into special stages works. Special stage 2: the robots need taps (homing attack) and the port had no way to do it; the "SPECIAL STAGE" banner (emerald result layout) came back mid-stage.
- Emerald result layout removed. Its signature 0x02306A20 is robot code: it runs in every frame of rpl-002 that shows Eggman's robots and in no frame of rpl-001 (no robots). The special stage keeps its layout through the result screen; no new result trigger until one is proven against recordings.
- Robot homing: the game's tap check is ov01 0x02312258 (player+0x114 allows it; touch record 0x022B65B4, new-tap bit 0x40 at +0x10, tap point +8/+0xA), which calls 0x02310174: walk the robots, skip by flags and the 0x02310248 distance window, project to the bottom screen, test the tap (0x0230AE34). A hit sets player state 0x12. Now A or B with no number prompt up asks for a homing attack: hooks report a new tap and count the first robot that passes the game's own checks as hit (12 frames at most). Host (special stage 2, A presses at 6640-6960M): three homing attacks, robots 021876B8, 02187964, 0218824C, explosions on screen.
- The leftover right-stick virtual stylus (ZR hold, R3 tap) is off.
- Host: NR_INPUT accepts TOUCHx.y (held touch); A presses request homing.

## 2026-11-20: switch-test35

- test34 result (Mark): backgrounds great in zones 1 and 2; pause fine; special stage "SPECIAL STAGE" banner came back mid-stage and blocked the circle taps until a pause; world map dialogue on the wrong screen; zone 3 sky below buildings; water line unchanged. Asked for a level select.
- Special stage banner: log 22:01 shows "emerald result" mid special stage (143 s, 248 s, 262 s); the test30 latch then held the result layout until the stage code stopped (a pause). Now the result layout needs the result code on 6 of the last 16 frames.
- Level select: every stage start goes through main 0x0205247C (Thumb; r0 zone 0..6, r1 0 act 1, 1 act 2, 2 boss; stores them at 0x022C457C/D, then 0x02055178 for acts or 0x0204888C for bosses). The stage table at 0x020745A8 (8 bytes per stage, 3 per zone: z11 z12 z13 ... z73, then z81 z82 zf3 z91 z92) confirms the numbering. A hook there replaces the zone and act with the port menu's armed choice (Level select: Zone, Stage, Go). Host: armed zone 3 boss, picked zone 2 act 1, the zone 3 boss loaded and the boss check held (third boss). Special stages (stage choice "Special stage"): the game enters one through the scene stub main 0x02055108 (overlay 1, entry 0x0230C9F4), reached from the act overlay (veneer ov00 0x0234CFBC) after a Special Generator launch; the entry takes the stage number from the zone byte 0x022C457C. A hook on the act start call (0x020524BC) calls that stub instead. Host: zone 2 armed, the special stage 2 intro came up directly from the world map. (An earlier attempt that moved Sonic next to a generator was dropped: generators were not created after the jump and some spots were pits.)
- World map dialogue: the cutscene code (0x02060E8D) also runs world map dialogues (rpl-017, dialogue on the bottom screen). A cutscene with no stage start since the world map now gets the world map layout; the pre-boss cutscene (after a stage start) keeps the top screen (host). World map case not reproduced on the host.
- Zone 3 backgrounds: host zone 3 act 1 (via the level select): each DS screen itself shows the far layer as a dark interior wall above a band of sky with obelisks, and the scaled view shows the same. Not changed; asking Mark whether the view should differ from the DS here.
- Water line: rpl-015's register log has no mid-frame 2D register writes; DMA3 feeds the 3D geometry FIFO (0x04000400) through the frame. Lead: the water is drawn by the 3D engine. Not investigated further.
- Host: NR_LVL=zone,act arms the level select.

## 2026-11-20: queued for the next build

- hbmenu icon: Mark's Sonic Rush art (2026-11-20). Kept outside the repo at /home/claude/private/icon/icon.jpg (256x256, never committed); build.sh passes it to CMake as SRNX_ICON when present, otherwise the repo's original icon is used. A public release needs an original icon instead (official art in a public GPL release).

## 2026-11-20: switch-test34

- test33 result (Mark, Leaf Forest): the background glide panned the wrong way (sky ended up below the forest while falling) and was too quick, still felt like a pop.
- Direction: every logged jump was exactly 240 rows. The two screens are 272 rows apart in the far background and the map wraps at 512, so the move to the lower screen read as -240 and the glide took that path (rpl-013 frames 71-100: forest entering from the top). The main screen's move in the view now sets the direction (down: +272).
- Speed: fixed 75 frames (1.25 s) with smoothstep instead of 1/12 of the remainder per frame. A new jump mid-glide restarts from the current position.

## 2026-11-20: switch-test33

- test32 result (Mark): backgrounds much improved, but they change suddenly at times. rpl-011: the far background flips from sky to deep forest at frame 93, the frame the main DS screen's place in the view jumps (-127 to +130: the game moved Sonic to the other screen). The scaled far layer takes the main screen's BG1 rows, and the two screens show different slices.
- Glide: a jump of more than 24 rows or columns in the main screen's BG1 scroll keeps the old slice and eases to the new one (1/12 of the remaining distance a frame, about half a second). Host: jumps of about 190 rows at act start and after a respawn; no stale rows seen during the act start glide. A mid-play screen swap was not reproduced on the host.
- Cutscene: with Mark's new save the host reaches the zone 2 pre-boss cutscene. Main program function 0x02060E8D runs on 1434 of its 1518 frames (the rest is the closing fade) and in none of 9104 other frames (menus, world map, cards, Leaf Forest and Water Palace play, zone 2 boss); now the cutscene's code signature (top screen layout). The last layout pixel check is removed. Host: scene 3 from 3646M to 5484M.
- Main program functions are now in the per-frame code record too (NR_OVSTAMP group 2, nr_ovfn_02, 2584 functions; overlays first in the list). Mark's two pre-boss cutscene replays recorded no overlay functions on any frame, so the cutscene runs in the main program; (superseded the same day: Mark's new save reached it, see above).
- Replays: stage frames the presenter has not drawn yet are left out (every stage replay ended with 2-3 black frames).

## 2026-11-19: switch-test32

- test31 result (Mark): in acts the picture flipped between widescreen and a narrower one with Sonic larger. Cause (log + code): the test19 presenter act card window compared the stage view with the DS screens for 15 s after each act start; with the scaled far background the view never matches the DS, the score sat at the 0.82 trigger (log: 0.80-0.90) and the window opened and closed repeatedly, swapping in the DS screen scaled to the view height.
- Removed: that window (act_card, draw_card, wide_mismatch_value), g_srnx_act_start, g_srnx_card_bottom, g_srnx_autocap, unused pixel helpers in srnx_special_detect. Cards are the code check only.
- Kept: the pre-boss cutscene pixel check (no code signature yet: its replay recorded no overlay functions) and find_circle (special stage tap target, not a layout check). menunav is the stub; the MPH boot splash skip is already off for this game.

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
