# SonicRush-nx 0.5.0

First release of SonicRush-nx: Sonic Rush (Nintendo DS, USA) as a native Nintendo Switch homebrew app. The game's DS code is translated ahead of time into Switch code (static recompilation), with its own deko3d renderer, sound and controls. It is not an emulator, and no game data is included: bring your own dump (see the README for the exact ROM).

## Highlights

- **Switch Port layout:** one 16:9 picture. Acts get a wide view of the level that follows Sonic; bosses, special stages, title cards, results, cutscenes, the world map and the menus each get their own one-screen layout, picked from the game's own code each frame.
- **Original stacked layout:** the two DS screens as on the DS, with the gap between them.
- **Special stages without a touch screen:** left stick steers, A or B taps the number prompts, homes in on robots and continues the intro.
- **Port menu (hold minus):** drawn over any layout. Stage zoom, show speed, level select (any zone, act, boss or special stage), CPU clock (Stock, 1224, 1428 or 1785 MHz; 1428 by default).
- 60 fps at the default 1428 MHz clock in play.

## Install

1. Copy the `switch` folder from the zip to the root of your SD card.
2. Put your Sonic Rush (USA) ROM in `/switch/sonicrush-nx/`.
3. Launch through title takeover (hold R while starting a game).

## Known limits

- Some visual glitches remain, mostly in the wide view's extra width.
- Some screens use both DS screens shrunk to fit rather than a true widescreen layout.
- Stock clock does not hold 60 fps in the busiest scenes yet.
- No DS wireless or online play. USA ROM version 0 only.

## Licence

GPL-3.0-or-later (the .nro links melonDS-derived code). Third-party notices are in the zip's `LICENSES` folder.

Sonic Rush and Sonic the Hedgehog are trademarks of SEGA. Not affiliated with or endorsed by SEGA or Nintendo.
