SonicRush-nx 0.5.0
==================

A native Nintendo Switch port of Sonic Rush (Nintendo DS). The game's DS code
was translated ahead of time into Switch code (static recompilation) and runs
as a homebrew app with its own renderer, sound and controls. It is not an
emulator.

No game data is included. You need your own dump of the game:

  Sonic Rush (USA) (En,Ja,Fr,De,Es,It), game code ASCE, ROM version 0
  untrimmed 64 MiB, SHA-1 bc617530bb9b6e4179f2e54d7816c38676812dde

The .nro holds translated program code only; graphics, sound, levels and text
are read from your ROM at launch.


Install
-------

1. Copy the "switch" folder from this zip to the root of your SD card.
2. Put your ROM in /switch/sonicrush-nx/ (any file name ending in .nds).
3. Launch through title takeover: hold R while starting any game, then pick
   SonicRush-nx from the homebrew menu. Applet mode (the Album) does not have
   enough memory.

Your game save is kept in /switch/sonicrush-nx/ next to the .nro.


Screen layouts (port menu, Display)
-----------------------------------

Switch Port (default): one 16:9 picture. In stages, a wide view that follows
Sonic, with the HUD on top. Bosses, special stages, act and boss title cards,
results, menus and the world map each get their own one-screen layout.

Original stacked: the two DS screens as on the DS, one above the other with
the gap between them.


Controls
--------

The Switch buttons work as the DS buttons in the same places (face buttons,
L and R, + for Start, D-pad or left stick for the D-pad).
Special stages: A or B taps the highlighted target, the way the DS stylus
would.

Hold - (minus) for half a second to open the port menu.


Port menu
---------

Display: screen layout, stage zoom (how much of the level the Switch Port
view shows), show speed.
Level select: pick a zone and act (or boss, or special stage); the next act
you start from the world map starts there instead.
Options: CPU clock (Stock, 1224, 1428 or 1785 MHz; 1428 by default, Stock
does not hold 60 fps in the busiest scenes yet), audio stretch.


Known limits
------------

- There are still visual glitches in places, mostly in the Switch Port
  layout's extra width beyond what the DS screens showed.
- Some screens are shown as both DS screens shrunk to fit rather than a
  true widescreen layout.
- A raised CPU clock is needed for a steady 60 fps.
- DS wireless and online play are not supported.
- USA ROM version 0 only.


Licence
-------

The .nro includes melonDS-derived code and is distributed under the GNU GPL,
version 3 or later. Third-party notices are in the LICENSES folder.

Sonic Rush and Sonic the Hedgehog are trademarks of SEGA. Nintendo DS and
Nintendo Switch are trademarks of Nintendo. This project is not affiliated
with or endorsed by SEGA or Nintendo.
