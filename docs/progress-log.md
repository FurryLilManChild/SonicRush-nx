# Progress log (newest first)

## 2026-11-22: test40 results, switch-test41 changes (not built yet)

- Mark, test40: act and boss cards better; bosses hold their layout; act results still broken; zone 4 robots break up as zone 3's did; raft capture taken (rpl-002 with its full file).
- Sprites: a checker (tools outside the repo: draw each sprite from its record with the sprite memory of nearby frames and score it against the DS screen of the same frame) on Mark's zone 4 captures: the flying robot's new shape and new pictures arrive in the same frame, so test40's one-frame-earlier pictures broke it, while zone 3's robot needs them. Timing is per object. test41 keeps both copies and gives each sprite whose pictures differ the copy that matches what the DS drew for it this frame; off the DS screen, the earlier copy if the same sprite (tile, shape, size) was drawn last frame, else the current one (29 of 32 decidable cases right in Mark's captures). Host zone 1 act 1: identical to test40 (which matched the DS there).
- Raft water (rpl-002 full file, redrawn offline): the water is one row of 32x64 sprites (tile 0x0B7, y 117) across the screen, ending at the DS screen's bottom edge; the per-line BG1 table is flat there (not the cause). test41: a row of 4 or more identical sprites reaching the DS screen's bottom edge has its bottom tile row repeated as 32x8 pieces down to the view's bottom. Offline: water to the bottom.
- Act clear: the stage view stayed on while the game made the main screen the results panel (rpl-003: stripes). Host zone 1 act 1 played to the goal (NR_TELEAT 20300,1560 then right): four main-program functions (0x02047459, 0x02047ECD, 0x020474F9, 0x02047665) run in all 572 results frames and in none of 16,960 other host frames. 3 of 4 in a frame = scene 15 (act clear), drawn as both screens shrunk like the card. Host: scene 15 from 4291M to the end; zone 1, 3, 4 acts and zone 3 boss sweep: unchanged.

## 2026-11-21: switch-test40

- Host sweep with level select: every act, boss and special stage (14 acts, 7 bosses, 7 special stages) to a few seconds of play, layout per frame logged, view sampled. Zone 3 act 2's title card flickered card / stage (3297M to 3612M); the zone 3 and zone 5 bosses fell out of the boss layout in the white fade after a death. Everything else switched as expected. Zone 7's boss stops in its pre-boss dialogue (no input to advance it).
- Card: once on, it stays while the stage runs with no play code. Boss: the boss layout holds for up to 180 frames after the boss code last ran over frames with no other screen's code (stage or card); the card never overrides the boss (test39 log, Mark: the ball hit). Host: both fixed.
- Results, zone 3 act 1 (Mark's rpl-002): the view sat at world y 0 with the main camera at y 2928, i.e. the camera target no longer gave Sonic's place. The view now follows the target only within 400 x 320 px of the main camera's screen, else holds the last good target. Lead, not confirmed: the host could not finish the act (Sonic placed by the goal does not trigger it).
- Sprites (Mark's rpl-003, frames 64, 72, 105, 113): the robot's new pose pictures reach sprite memory one frame before its new layout reaches the records. The robot redrawn from the frame's records with the previous frame's sprite memory is whole; with its own frame's memory it is in pieces, as on screen. The view now draws with the previous frame's sprite memory. Host zone 1 act 1: Sonic's dash effect and the birds' wing frames now match the DS screen of the same frame (before, a frame off).
- Raft water (Mark's rpl-004): not fixed. Zone 3 moves BG1 per line by HBlank DMA from two tables (0x02227998 / 0x02227F98 engine A, swapped each frame). Using the table in the scaled far background put water at the wrong height (reverted). Host, zone 3 act 2 near the raft: with BG1, BG2 or BG3 turned off in the view the water bands remain, so they are not from those layers; open.
- Raft water, host (zone 3 act 2 near the raft, NR_WIDE_NOBG): the water surface strips are sprites (gone with OBJ off, not with BG1, BG2, BG3 or the main screen paste off); the flat water below them is BG1. The host could not reach the raft itself (placed on the raft object, type 124 at 18776,4360, Sonic dies).
- Replays now also write rpl-NNN-full.bin: the last 40 stage frames' whole view snapshot (all but the 3D layer, about 1.5 MB a frame) with engine A's per-line BG1 scroll table (HBlank DMA source from io.cpp nds_dma9_hblank_src, patch). The host redraws them offline (NR_FULLRENDER=file,stem), so a fix can be tried on Mark's exact frames. Host check: NR_REPLAYAT records and saves on the host; zone 3 act 1, 40 frames, 61 MB, redrawn.
- Host tools: NR_TELEAT (place the player and both cameras), NR_WIDE_NOBG (drop BG layers, 4 = sprites, from the dumped view), NR_REPLAYAT, NR_FULLRENDER.

## 2026-11-21: switch-test39 (diagnostic)

- test38 result (Mark): the level-select emerald saved. The zone 3 act 1 death froze again (no special stage before it this time). The wild-call log: task loop main 0x02036E70 runs the object update main 0x0206C3D0 with r5 = 0x0236ACA0, the first byte after the act overlay's bss (ov00 0x022C5C80 + 0xA3E40 + bss 0x11E0); it calls the object's callback [r5+0x6C] = 0xBBBBBBBB (lr 0x0206C4B8). In the host zone 2 and zone 3 runs that memory is all zero and nothing writes it (zone 3 act 1 with a death: no writes). Who allocated an object there and who overwrote it is not known.
- Diagnostic: stores into 0x0236ACA0..0x0236AE00 go through the slow path (that page only); every CPU store (with the last entered function, NR_OVSTAMP now also records it), native SWI store and DMA write (with its source) goes into a 128-entry ring; any store to the callback (+0x6C) is logged at once; on a wild call the ring and the object are printed. Checked on the host on the player record (NR_SWATCH): writes and functions logged.

## 2026-11-21: switch-test38

- test37 result (Mark): world map after level-select special stages works, results layout correct, robots and prompts fine. The emerald won from a level-select special stage was not on the world map. After a generator special stage in zone 3 act 1 and about 90 s of play, a death led to the respawn card, then calls into 0xBBBBBBBA and onward (unmapped reads at 0xBBB9xxxx), 1.7 fps, white screen. Not reproduced on the host (deaths in zone 2 and 3 act 1, and after an act resume, respawn fine); cause unknown.
- Emerald: the special stage teardown (ov01 0x0230C104) sets the won stage's bit in the working mask 0x022C4588 (0x0230C204..0x0230C224). The saved mask is save record byte 0x022C468F (main 0x02059874 reads it; the act copies it over the working mask at ov00 0x0234D02C; the world map panel shows it, host: 0x7F there shows seven). A level-select special stage now also puts the bit in 0x022C468F and calls the game's save routine main 0x0205A4EC (as ov01 0x023002B4 does). Host (NR_SPWIN forces the win): special stage 4, mask 03 -> 0B, the save file's record byte 0x1B went 3 -> 11 in both copies, world map shows the third emerald.
- Diagnostics: a call outside RAM logs lr, r0-r7 and 16 stack words (first 4), and saves a replay once.

## 2026-11-21: switch-test37

- test36 result (Mark): robot homing works, number prompts still work, no banner mid-stage. After a level-select special stage the result screen ended in a white screen that stayed (log: game running, no overlay code, vblank waits only). Reproduced on the host.
- Cause (game code, host and log agree): entering from an act, the game sets bit 0x40 of 0x022C4578 (ov00 0x02343EC8) before the scene switch; the special stage's end (ov01 0x0230BC70) tests it and calls the scene stub 0x02055194 (overlay 0, 0x0204D7E0) to resume the act at the place saved on the way in. The level select skips the act, so the bit was clear and nothing followed. Setting the bit (host) brought zone 2 act 1 back with Sonic at 1,0, off the map. Fix: a level-select special stage ends on the world map (hook ov01 0x0230BC84 calls main 0x02054FD4, the world map stub the act and the main menu use). Host: special stage 2, robots, end, world map.
- Emerald result layout back, from Mark's result capture (rpl-001, 120 frames): 11 overlay 1 functions run in every result frame and in none of rpl-001/rpl-002 of test35 (240 frames) or about 12000 host frames (world map, menus, intro, play with robot homing, a failed finish). One (0x02307148) is a spawn helper, so the layout needs 6 of the 11 in a frame. Host special stage 2 run: never triggered. The real result screen is checked only by Mark's capture (no win on the host).

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
