# Progress log (newest first)

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
