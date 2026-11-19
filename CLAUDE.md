# SonicRush-nx

Native Nintendo Switch homebrew port of Sonic Rush (Nintendo DS, USA, ASCE) as a native static recompilation: the game's ARM9 and ARM7 code translated to C by our own generator (`tools/nrecomp`), running as real Switch code on a hardware layer taken from ndsrecomp and a deko3d renderer. Built from MPH-nx (Metroid Prime: Hunters), the reference DS port.

Goal: a single-screen, modern widescreen presentation. Phase 1 joins both DS screens into one seamless column; phase 2 reworks the stage view into true 16:9 (about 683x384 of the level, same height as the two DS screens together). See `docs/design.md`.

## Who does what

- Mark writes no code. Claude writes all code, scripts and docs.
- Mark tests on his Switch (unpatched V1, Atmosphere, sys-clk) and PC (Lenovo Legion Go), and drops logs in `C:\SonicRush-nx\out\sd\switch\switchlogs`.
- Switch NRO builds are made in the cloud. Cloud boot tests are approved for this project (2026-11-14).
- Until a build reaches the title screen: build and fix without asking, post an update at least every 10 minutes. After that, confirm with Mark before every build, and after reading test logs present findings and the plan, then wait for his go-ahead.
- Deliver each Switch test by replacing the .nro in `C:\SonicRush-nx\out\sd\switch\sonicrush-nx\`.

## Hard rules

- No em dashes anywhere (chat, docs, commits, release notes). No decorative emoji.
- Never commit game data: ROM, saves, generated code (`nrgen/`), BIOS/firmware, captures. Run `git status` before every commit.
- Commit as `FurryLilManChild <346934328+FurryLilManChild@users.noreply.github.com>`.
- Never ship a Switch build without its renderer, sound, input or game code. Check the artifact before handing it over.
- Upstream sources stay on pinned commits; changes to ndsrecomp live in `switch/patches/0001-ndsrecomp-switch-portability.patch`.
- WE pick the path. Upstreams are resources, never the design. Say plainly when a part is still emulation-style.
- Never present a theory as a known cause. Be honest about what each test is for and what Mark should see.
- Before naming the cause of a hang or wrong behaviour: disassemble the code at every address the log shows the game stuck on and find what it waits for, then read the port code that should answer that wait. Only when the game's code, the port's code and the log agree is it a cause; anything less is a lead and is called one. (switch-test21: an untranslated function was named as the cutscene hang's cause without looking at the loop; the loop was waiting on DISPSTAT's HBlank bit, which the port never set.)

## Required ROM

Sonic Rush (USA) (En,Ja,Fr,De,Es,It), game code ASCE, ROM version 0, 64 MiB untrimmed, SHA-1 `bc617530bb9b6e4179f2e54d7816c38676812dde`. Header and logo CRCs check out. ARM9 uncompressed, 2 ARM9 overlays (both at 0x022C5C80), no ARM7 overlays.

## Upstream pins

- Engine: RetroPortingToolKit/ndsrecomp, release/native-hle-v0.6.11, 179ac63d9e4055e1218d61b3fd03c34a94880a6e (submodules: arm-recomp-core 90b6c51, freebios 6d6cfb4)
- recomp-ui: mstan/recomp-ui 3f1d6f1 (through mstan/MetroidPrimeHuntersRecomp 61ac84a)
- Reference port: FurryLilManChild/MPH-nx d3b4f5d
- Reference for the engine family: RushRE/SonicRushAdventure-Decomp (sequel; structure only)

## Layout

- `docs/design.md`: goal, pieces, phases, open decisions
- `docs/progress-log.md`: newest first, one entry per session
- `docs/upstream.md`: upstream pins, licenses, what we take
- `tools/nrecomp/`: native generator (rom, decode, analyze, gen, gen_all, gen_all7, missroots, rootfilter, addrmap)
- `switch/`: Switch platform layer, native runtime, deko3d renderer, patches, build scripts; `switch/host/` is the headless x86-64 boot test
