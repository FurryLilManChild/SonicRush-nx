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

Original stacked: the two DS screens as on the DS, one above the other with
the gap between them, everywhere in the game.

Switch Port (default): one 16:9 picture, chosen screen by screen. The port
recognises which screen is up from the game's own code running that frame
(not from the pixels), and switches the layout to match:

- Acts: a wide view of the level that follows Sonic, drawn from the game's
  own level, sprites and 3D, with the HUD on top. Display > Stage zoom sets
  how much of the level it shows.
- Water and other strips the DS only drew down to its screen's bottom edge
  are carried on to the bottom of the wide view.
- Act and boss title cards, the "ready" card after a life is lost, and the
  act results (score tally): both DS screens shrunk into one tall picture.
- Bosses: the arena (the top screen) fills the picture, with the boss's
  health bar upright at the side. The boss layout holds through the whole
  fight, including the white fade after a lost life and the moments the
  boss pauses; only another screen's own code can end it.
- Special stages: the half-pipe in the middle, rings and the goal in the
  left bar, progress in the right bar.
- Special stage intro and the emerald result: the bottom screen, with the
  banner and the emerald ring placed beside it.
- Cutscenes: the top screen fills the picture.
- World map: the map at full height, the status panel in a corner.
- Title screen, main menu, character select, logos and anything not listed:
  both screens as one tall picture, trimmed to what matters on each.

The port menu and the speed counter are drawn over whatever layout is
showing.


Controls
--------

The Switch buttons work as the DS buttons in the same places (face buttons,
L and R, + for Start, D-pad or left stick for the D-pad).

Hold - (minus) for half a second to open the port menu.


Special stages (the DS played these with the stylus)
------------------------------------------------------

- Steer: left stick. Sonic goes to the point of the half-pipe the stick
  points at: stick down keeps him at the bottom middle, left or right moves
  him up that side.
- Number prompts during a jump (the numbered circles): press A or B to tap
  the circle the game has lit up next. Press once per circle, in time.
- Robots: press A or B (with no number prompt on screen) to homing-attack
  the robot the game targets.
- "Touch to continue" on the special stage intro: A or B.
- Everything else uses the buttons as above.

No other part of Sonic Rush needs the touch screen.


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
