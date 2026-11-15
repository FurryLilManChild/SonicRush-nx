# Upstreams

| Project | Pin | License | What we take |
|---|---|---|---|
| RetroPortingToolKit/ndsrecomp | 179ac63 (release/native-hle-v0.6.11) | runner and ARMv4T core MIT; melonDS-derived parts GPL-3.0-or-later; recompiler tool PolyForm Noncommercial | hardware layer, SDL frontend, FreeBIOS, save states, interpreter fallback; nds_recompile only for the FreeBIOS banks |
| mstan/recomp-ui | 3f1d6f1 (via mstan/MetroidPrimeHuntersRecomp 61ac84a) | see repo | frontend UI library the runner links |
| mstan/freebios | 6d6cfb4 | BSD-style | the BIOS |
| FurryLilManChild/MPH-nx | d3b4f5d | GPL-3.0-or-later | native generator, runtimes, Switch layer, deko3d renderer, presenter |
| RushRE/SonicRushAdventure-Decomp | not pinned | see repo | reference for the engine family's structure only; no code |

The shipped .nro links melonDS-derived code, so the whole .nro is GPL-3.0-or-later.
